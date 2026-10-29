#include "cwxeditor_src/core/cwx/cwl.hpp"

#include "cwxeditor_src/core/cwx/area.hpp"
#include "cwxeditor_src/core/cwx/background.hpp"
#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/event.hpp"
#include "cwxeditor_src/core/cwx/filesync.hpp"
#include "cwxeditor_src/core/cwx/imagesize.hpp"
#include "cwxeditor_src/core/cwx/motion.hpp"
#include "cwxeditor_src/core/cwx/msgutils.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/perf.hpp"
#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/core/cwx/sjis.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/xml.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <array>
#include <cmath>
#include <cctype>
#include <charconv>
#include <exception>
#include <filesystem>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>

namespace cwxeditor::cwx::cwl {
namespace {

lib::Array<ConstOwnerBgImage> snapshotBackgrounds(
    const lib::ConstOwnerSlice<BgImage>& cells) {
    lib::Array<ConstOwnerBgImage> result;
    result.reserve(cells.size());
    for (const auto& cell : cells) result.push_back(cell);
    return result;
}

struct ReadVersionScope {
    RData& data;
    decltype(RData::dataVersion) previous;
    explicit ReadVersionScope(RData& value) : data(value), previous(value.dataVersion) {}
    ~ReadVersionScope() { data.dataVersion = previous; }
};

struct ClassicReferenceConversionError : std::invalid_argument {
    using std::invalid_argument::invalid_argument;
};

template <typename T>
[[nodiscard]] T parseUnsigned(StringView value) {
    // Dのto!uint/ulongは先頭の符号と末尾の余分な文字を
    // 拒否する。変換失敗と、参照関係の変更が送出する例外は区別する。
    const auto result = utils::detail::parseDInteger<T>(value);
    if (!result) throw ClassicReferenceConversionError("Invalid unsigned classic reference");
    return *result;
}

template <typename T>
[[nodiscard]] lib::Array<lib::Owner<T>> sortedVariables(
    const lib::Array<lib::Owner<T>>& values, bool logical) {
    lib::Array<lib::Owner<T>> result;
    flag::sortedWithPath(values, logical, [&result](const auto& value) { result.push_back(value); });
    return result;
}

[[nodiscard]] std::invalid_argument invalidValue(StringView what,
                                                  std::uint32_t value) {
    return std::invalid_argument(String(what) + ": " +
                                  std::to_string(value));
}

[[nodiscard]] String replaceAll(String value, StringView from, StringView to) {
    if (from.empty()) {
        return value;
    }
    std::size_t pos = 0;
    while ((pos = value.find(from, pos)) != String::npos) {
        value.replace(pos, from.size(), to);
        pos += to.size();
    }
    return value;
}

[[nodiscard]] bool asciiIEqual(StringView left, StringView right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto a = static_cast<unsigned char>(left[i]);
        const auto b = static_cast<unsigned char>(right[i]);
        if (std::tolower(a) != std::tolower(b)) return false;
    }
    return true;
}

[[nodiscard]] lib::Array<String> splitLines(StringView value) {
    // DのreadStrings/readExStringsは両方ともstd.string.splitLinesを使う。
    return utils::splitLines(value);
}

[[nodiscard]] String byteString(binary::ByteView bytes) {
    return String(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

[[nodiscard]] bool endsWith(binary::ByteView value, StringView suffix) {
    if (value.size() < suffix.size()) {
        return false;
    }
    const auto start = value.size() - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        if (value[start + i] != static_cast<Byte>(suffix[i])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] String normalizeClassicPath(String value) {
    return replaceAll(std::move(value), "/", "\\");
}

[[nodiscard]] String normalizeHostPath(String value) {
    return replaceAll(std::move(value), "\\", "/");
}

[[nodiscard]] std::filesystem::path fsPath(StringView value) {
    const std::u8string encoded(
        reinterpret_cast<const char8_t*>(value.data()), value.size());
    return std::filesystem::path(encoded);
}

[[nodiscard]] bool classicPathExists(const std::filesystem::path& value) {
    // Dのstd.file.existsはGetFileAttributes/lstat
    // を使う。参照先のないリンクも存在として数え、属性取得の失敗時はfalseを返す。
    std::error_code error;
    const auto status = std::filesystem::symlink_status(value, error);
    return !error && std::filesystem::exists(status);
}

[[nodiscard]] String utf8Path(const std::filesystem::path& value) {
    const auto encoded = value.generic_u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

[[nodiscard]] std::uint32_t checkedLength(std::size_t value) {
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        // CWLの長さの接頭値は32ビット。Dは過大な長さを
        // キャストしながら内容を全て書くため、次のレコードの
        // 開始位置がずれる。破損データを書かずに拒否する。
        throw std::length_error("classic field exceeds uint32 length");
    }
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] String decodeClassicString(binary::ByteView bytes) {
    String raw = byteString(bytes);
    try {
        return sjis::touni(raw);
    } catch (const std::exception&) {
        return sjis::touni(raw, false);
    }
}

[[nodiscard]] String imagePathForRead(const RData& d, StringView path) {
    if (path.empty() || utils::isBinImg(path)) {
        return String(path);
    }
    return normalizeHostPath(String(path));
}

[[nodiscard]] Byte enumByte(std::int32_t value) {
    if (value < 0 || value > 255) {
        throw std::invalid_argument("enum value outside classic byte range");
    }
    return static_cast<Byte>(value);
}

[[nodiscard]] std::uint32_t readClassicCount(ByteIO& f, std::int32_t version) {
    return version < 7 ? f.readUIntL() : static_cast<std::uint32_t>(f.readExUInt());
}

[[nodiscard]] std::int32_t readClassicSigned(ByteIO& f, std::int32_t version) {
    return version < 7 ? f.readIntL() : f.readExInt();
}

[[nodiscard]] std::uint64_t readClassicId(ByteIO& f, std::int32_t version) {
    return version < 7 ? f.readUIntL() : static_cast<std::uint32_t>(f.readExUInt());
}

[[nodiscard]] String readClassicText(ByteIO& f, std::int32_t version,
                                     bool lns = false) {
    return version < 7 ? readString(f, lns) : readExString(f, lns);
}

[[nodiscard]] std::uint32_t readCellWidth(ByteIO& f, std::uint32_t& version) {
    auto width = f.readUIntL();
    if (width >= 70000U) {
        version = 7;
        width -= 70000U;
    } else if (width >= 60000U) {
        version = 6;
        width -= 60000U;
    } else if (width >= 40000U) {
        version = 4;
        width -= 40000U;
    }
    return width;
}

[[nodiscard]] structs::CRGB readRgb(ByteIO& f) {
    // Classicとモデルは、どちらもこれらの項目をRGBA順で保存する。
    const auto r = f.readUByte();
    const auto g = f.readUByte();
    const auto b = f.readUByte();
    const auto a = f.readUByte();
    return structs::CRGB{r, g, b, a};
}

[[nodiscard]] structs::CRGB readBgr(ByteIO& f) {
    const auto b = f.readUByte();
    const auto g = f.readUByte();
    const auto r = f.readUByte();
    const auto a = f.readUByte();
    return {r, g, b, a};
}


void setCellCommon(const lib::Owner<BgImage>& cell, bool foreground,
                   String cellName) {
    if (!cell) return;
    cell->layer(foreground ? types::LAYER_FORE_CELL
                           : background::BgImage::LAYER_BACK_CELL);
    cell->cellName(std::move(cellName));
}

}  // namespace

bool sWith(StringView file, StringView prefix, std::uint64_t& id) {
    id = 0;
    if (file.size() <= prefix.size() + 4 ||
        !utils::cfnmatch(file.substr(0, prefix.size()), prefix) ||
        !utils::cfnmatch(file.substr(file.size() - 4), ".wid")) {
        return false;
    }
    const auto number = file.substr(prefix.size(), file.size() - prefix.size() - 4);
    // DはまずPhobosの数値表記を受け付け、その後ulongへ変換
    // する。整数でない数値は、別のファイルのように扱わず例外とする。
    if (!utils::isNumeric(number)) return false;
    id = parseUnsigned<std::uint64_t>(number);
    return true;
}

String encodePathLegacy(String path) {
    return utils::isBinImg(path) ? std::move(path)
                                 : normalizeClassicPath(std::move(path));
}

String encodePathRes(const SData& d, String path, StringView resourceExtension,
                     const lib::Array<String>& resourceDirs,
                     const lib::Array<String>& resourceExtensions) {
    path = encodePathLegacy(std::move(path));
    // C++では保存内容の構築中に未接続のSDataを許可する。Dは非nullのopt/skin
    // を必要とするが、C++でホストの接続がない場合は参照を避ける。
    if (!d.opt || !d.skin || !d.opt->replaceClassicResourceExtension ||
        (d.skin->legacy() && d.opt->dataVersion < 7) ||
        d.skin->sourceOfMaterialsIsClassicEngine()) {
        return path;
    }
    auto dot = path.find_last_of('.');
    const auto ext = dot == String::npos ? String{} : path.substr(dot);
    String lower = ext;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    bool listed = false;
    for (const auto& candidate : resourceExtensions) {
        if (candidate == lower) { listed = true; break; }
    }
    if (!listed || lower == String(resourceExtension)) return path;
    bool skinMaterial = false;
    bool engineMaterial = false;
    (void)d.skin->findPathF(path, resourceExtensions, resourceDirs, d.sPath,
                              versioninfo::LATEST_VERSION, {}, skinMaterial, engineMaterial);
    if (skinMaterial) {
        if (dot == String::npos) path += String(resourceExtension);
        else path.replace(dot, String::npos, String(resourceExtension));
    }
    return path;
}

String encodePathTable(const SData& d, String path) {
    if (!d.skin || !d.opt) return encodePathLegacy(std::move(path));
    return encodePathRes(d, std::move(path), d.opt->tableExtension,
                         d.skin->tableDirs(), d.skin->extImage());
}

String encodePathMidi(const SData& d, String path) {
    if (!d.skin || !d.opt) return encodePathLegacy(std::move(path));
    return encodePathRes(d, std::move(path), d.opt->midiExtension,
                         d.skin->bgmDirs(), d.skin->extBgm());
}

String encodePathWave(const SData& d, String path) {
    if (!d.skin || !d.opt) return encodePathLegacy(std::move(path));
    return encodePathRes(d, std::move(path), d.opt->waveExtension,
                         d.skin->seDirs(), d.skin->extSound());
}

String decodePathLegacy(String path) {
    return utils::isBinImg(path) ? std::move(path)
                                 : normalizeHostPath(std::move(path));
}

RData::RData(const CProps& props, bool cardsOnly, String scenarioPath,
             String type, String name, std::uint32_t threshold)
    : prop(&props),
      cardOnly(cardsOnly),
      sPath(std::move(scenarioPath)),
      skinType(std::move(type)),
      skinName(std::move(name)),
      numberStepToVariantThreshold(threshold) {}

RData::RData(const RData& source)
    : prop(source.prop), cardOnly(source.cardOnly), sPath(source.sPath),
      skinType(source.skinType), skinName(source.skinName),
      numberStepToVariantThreshold(source.numberStepToVariantThreshold),
      dataVersion(0), numberSteps(source.numberSteps), stepNames(source.stepNames) {
    // DのRData(d)は新しいファイルバージョンの状態を開始し、二つの
    // マップを独立にコピーする。以前に復号したバージョンは引き継がない。
}

ResourceType getType(StringView file, std::uint64_t& id) {
    const auto base = utf8Path(fsPath(file).filename());
    const std::array<std::pair<StringView, ResourceType>, 8> types{{
        {"Area", ResourceType::Area},       {"Battle", ResourceType::Battle},
        {"Package", ResourceType::Package}, {"Mate", ResourceType::Cast},
        {"Skill", ResourceType::Skill},     {"Item", ResourceType::Item},
        {"Beast", ResourceType::Beast},     {"Info", ResourceType::Info},
    }};
    for (const auto& [prefix, type] : types) {
        if (sWith(base, prefix, id)) {
            return type;
        }
    }
    id = 0;
    return ResourceType::Unknown;
}

types::Target toTargetT(Byte b) {
    switch (b) {
    case 0:
    case 0xFF:
        return {types::Target::M::Selected, false};
    case 1:
        return {types::Target::M::Random, false};
    case 2:
        return {types::Target::M::Unselected, false};
    default:
        throw invalidValue("Unknown target T", b);
    }
}

types::Target toTargetA(Byte b) {
    switch (b) {
    case 0: return {types::Target::M::Selected, false};
    case 1: return {types::Target::M::Random, false};
    case 2: return {types::Target::M::Party, false};
    case 3: return {types::Target::M::Selected, true};
    case 4: return {types::Target::M::Random, true};
    case 5: return {types::Target::M::Party, true};
    case 6: return {types::Target::M::Party, false};
    default: throw invalidValue("Unknown target A", b);
    }
}

types::EffectType toEffectType(Byte b) {
    switch (b) {
    case 0: return types::EffectType::Physic;
    case 1: return types::EffectType::Magic;
    case 2: return types::EffectType::MagicalPhysic;
    case 3: return types::EffectType::PhysicalMagic;
    case 4: return types::EffectType::None;
    default: throw invalidValue("Unknown effect type", b);
    }
}

types::Resist toResist(Byte b) {
    switch (b) {
    case 0: return types::Resist::Avoid;
    case 1: return types::Resist::Resist;
    case 2: return types::Resist::Unfail;
    default: throw invalidValue("Unknown resist", b);
    }
}

types::CardVisual toCardVisual(Byte b) {
    switch (b) {
    case 0: return types::CardVisual::None;
    case 1: return types::CardVisual::Reverse;
    case 2: return types::CardVisual::Horizontal;
    case 3: return types::CardVisual::Vertical;
    default: throw invalidValue("Unknown card visual", b);
    }
}

types::Range toRange(const RData& d, Byte b, Content& content, std::int32_t dataVersion) {
    if (dataVersion >= 7) {
        if (b == 6) b = 3;
        if (b == 7) {
            static const msgs::Msgs defaults;
            const auto& messages = d.prop ? d.prop->requiredMsgs() : defaults;
            content.comment(utils::tryFormat(static_cast<String>(messages.convertFromNextWarning),
                static_cast<String>(messages.convertFromNextWarningBackpackOfSelectedMember)));
            b = 0;
        }
    }
    switch (b) {
    case 0: return types::Range::Selected;
    case 1: return types::Range::Random;
    case 2: return types::Range::Party;
    case 3: return types::Range::Backpack;
    case 4: return types::Range::PartyAndBackpack;
    case 5: return types::Range::Field;
    default: throw invalidValue("Unknown range", b);
    }
}

types::Range toRangeE(Byte b) {
    switch (b) {
    case 0:
    case 3: return types::Range::Selected;
    case 1:
    case 4: return types::Range::Random;
    case 2:
    case 5:
    case 6: return types::Range::Party;
    default: throw invalidValue("Unknown range E", b);
    }
}

types::Range toRangeEffectContent(Byte b) {
    switch (b) {
    case 0: return types::Range::Selected;
    case 1: return types::Range::Random;
    case 2: return types::Range::Party;
    case 3: return types::Range::CardTarget;
    default: throw invalidValue("Unknown range EffectContent", b);
    }
}

types::Range toKeyCodeRange(const RData& d, Byte b, Content& content,
                            std::int32_t dataVersion) {
    if (dataVersion >= 7) {
        if (b == 4) b = 2;
        if (b == 5) {
            static const msgs::Msgs defaults;
            const auto& messages = d.prop ? d.prop->requiredMsgs() : defaults;
            content.comment(utils::tryFormat(static_cast<String>(messages.convertFromNextWarning),
                static_cast<String>(messages.convertFromNextWarningBackpackOfSelectedMember)));
            b = 0;
        }
    }
    switch (b) {
    case 0: return types::Range::Selected;
    case 1: return types::Range::Random;
    case 2: return types::Range::Backpack;
    case 3: return types::Range::PartyAndBackpack;
    default: throw invalidValue("Unknown key-code range", b);
    }
}

types::Range toCouponRange(Byte b) {
    switch (b) {
    case 0: return types::Range::Selected;
    case 1: return types::Range::Random;
    case 2: return types::Range::Party;
    case 3: return types::Range::Field;
    default: throw invalidValue("Unknown coupon range", b);
    }
}

lib::Array<types::CastRange> toCastRanges(Byte b) {
    lib::Array<types::CastRange> result;
    if (b & 0x01) result.push_back(types::CastRange::Party);
    if (b & 0x02) result.push_back(types::CastRange::Enemy);
    if (b & 0x04) result.push_back(types::CastRange::Npc);
    return result;
}

types::EffectCardType toEffectCardType(Byte b) {
    switch (b) {
    case 0: return types::EffectCardType::All;
    case 1: return types::EffectCardType::Skill;
    case 2: return types::EffectCardType::Item;
    case 3: return types::EffectCardType::Beast;
    default: throw invalidValue("Unknown effect-card type", b);
    }
}

types::Comparison4 toComparison4(Byte b) {
    switch (b) {
    case 0: return types::Comparison4::Eq;
    case 1: return types::Comparison4::Ne;
    case 2: return types::Comparison4::Lt;
    case 3: return types::Comparison4::Gt;
    default: throw invalidValue("Unknown four-way comparison", b);
    }
}

types::Comparison3 toComparison3(Byte b) {
    switch (b) {
    case 0: return types::Comparison3::Eq;
    case 1: return types::Comparison3::Lt;
    case 2: return types::Comparison3::Gt;
    default: throw invalidValue("Unknown three-way comparison", b);
    }
}

types::BorderingType toBorderingType(Byte b) {
    switch (b) {
    case 0: return types::BorderingType::Outline;
    case 1: return types::BorderingType::Inline;
    default: throw invalidValue("Unknown bordering type", b);
    }
}

types::BlendMode toBlendMode(Byte b, bool& mask) {
    mask = false;
    switch (b) {
    case 0: return types::BlendMode::Normal;
    case 1: mask = true; return types::BlendMode::Normal;
    case 2: return types::BlendMode::Add;
    case 3: return types::BlendMode::Subtract;
    case 4: return types::BlendMode::Multiply;
    default: throw invalidValue("Unknown blend mode", b);
    }
}

types::GradientDir toGradientDir(Byte b) {
    switch (b) {
    case 0: return types::GradientDir::None;
    case 1: return types::GradientDir::LeftToRight;
    case 2: return types::GradientDir::TopToBottom;
    default: throw invalidValue("Unknown gradient direction", b);
    }
}

types::CoordinateType toCoordinateType(Byte b) {
    switch (b) {
    case 0: return types::CoordinateType::Absolute;
    case 1: return types::CoordinateType::Relative;
    case 2: return types::CoordinateType::Percentage;
    default: throw invalidValue("Unknown coordinate type", b);
    }
}

types::Status toStatus(Byte b) {
    if (b <= 26) {
        return static_cast<types::Status>(b);
    }
    throw invalidValue("Unknown status", b);
}

types::Element toElement(Byte b) {
    if (b <= 6) return static_cast<types::Element>(b);
    throw invalidValue("Unknown element", b);
}

types::DamageType toDamageType(Byte b) {
    switch (b) {
    case 0: return types::DamageType::LevelRatio;
    case 1: return types::DamageType::Normal;
    case 2: return types::DamageType::Max;
    default: throw invalidValue("Unknown damage type", b);
    }
}

types::Physical toPhysical(std::uint32_t b) {
    if (b <= 5) return static_cast<types::Physical>(b);
    throw invalidValue("Unknown physical attribute", b);
}

types::Mental toMental(std::int32_t b) {
    switch (b) {
    case 1: return types::Mental::Aggressive;
    case 2: return types::Mental::Cheerful;
    case 3: return types::Mental::Brave;
    case 4: return types::Mental::Cautious;
    case 5: return types::Mental::Trickish;
    case -1: return types::Mental::Unaggressive;
    case -2: return types::Mental::Uncheerful;
    case -3: return types::Mental::Unbrave;
    case -4: return types::Mental::Uncautious;
    case -5: return types::Mental::Untrickish;
    default: throw std::invalid_argument("Unknown mental attribute: " +
                                         std::to_string(b));
    }
}

types::Mentality toMentality(Byte b) {
    if (b <= 5) return static_cast<types::Mentality>(b);
    throw invalidValue("Unknown mentality", b);
}

types::CardTarget toCardTarget(Byte b) {
    if (b <= 4) return static_cast<types::CardTarget>(b);
    throw invalidValue("Unknown card target", b);
}

types::Premium toPremium(Byte b) {
    if (b <= 2) return static_cast<types::Premium>(b);
    throw invalidValue("Unknown card premium", b);
}

bool readBool(ByteIO& f) {
    return f.readUByteL() != 0;
}

String readImageImpl(const RData& d, ByteIO& f,
                     const std::function<std::uint32_t()>& readSize) {
    const auto length = readSize();
    if (length == 0) return {};
    const auto image = f.read(length);
    if (endsWith(image, B_IMG_REF)) {
        std::size_t start = image.size() - B_IMG_REF.size();
        while (start != 0 && image[start - 1] != 0) --start;
        if (start != 0) {
            const auto ref = byteString(image.subspan(start, image.size() - start - B_IMG_REF.size()));
            const auto full = fsPath(utils::buildPath(d.sPath, ref));
            if (classicPathExists(full)) return ref;
            if (start > 0) return utils::bImgToStr(image.first(start - 1));
        }
    }
    return utils::bImgToStr(image);
}

String readExImage(const RData& d, ByteIO& f) {
    return readImageImpl(d, f, [&f] { return static_cast<std::uint32_t>(f.readExUInt()); });
}

String readImage(const RData& d, ByteIO& f) {
    return readImageImpl(d, f, [&f] { return f.readUIntL(); });
}

String readStringImpl(ByteIO& f, bool lns, bool cutText,
                      const std::function<std::uint32_t()>& readSize) {
    const auto length = readSize();
    if (length == 0) return {};
    String raw = byteString(f.read(length));
    if (!lns && !raw.empty() && raw.back() == '\0') raw.pop_back();
    const auto zero = raw.find('\0');
    if (zero != String::npos) raw.erase(0, zero + 1);
    String value = decodeClassicString(binary::ByteView(
        reinterpret_cast<const Byte*>(raw.data()), raw.size()));
    if (cutText) {
        constexpr StringView marker = "TEXT\r\n";
        value = value.size() > marker.size() ? value.substr(marker.size()) : String{};
    }
    return replaceAll(std::move(value), "\r\n", "\n");
}

String readExString(ByteIO& f, bool lns, bool cutText) {
    return readStringImpl(f, lns, cutText,
                          [&f] { return static_cast<std::uint32_t>(f.readExUInt()); });
}

String readString(ByteIO& f, bool lns, bool cutText) {
    return readStringImpl(f, lns, cutText, [&f] { return f.readUIntL(); });
}

String readString(ByteIO& f, lib::Dictionary<String, String>& addInfo,
                  bool lns, bool cutText) {
    const auto length = f.readUIntL();
    if (length == 0) return {};
    String raw = byteString(f.read(length));
    if (!lns && !raw.empty() && raw.back() == '\0') raw.pop_back();
    const auto firstZero = raw.find('\0');
    if (firstZero != String::npos) {
        const auto info = replaceAll(sjis::touni(StringView(raw).substr(firstZero + 1), false), "\r\n", "\n");
        const auto colon = info.find(':');
        addInfo[info.substr(0, colon)] = colon == String::npos ? String{} : info.substr(colon + 1);
        raw.resize(firstZero);
    }
    String value = decodeClassicString(binary::ByteView(
        reinterpret_cast<const Byte*>(raw.data()), raw.size()));
    if (cutText) {
        constexpr StringView marker = "TEXT\r\n";
        value = value.size() > marker.size() ? value.substr(marker.size()) : String{};
    }
    return replaceAll(std::move(value), "\r\n", "\n");
}

lib::Array<String> readExStrings(ByteIO& f) {
    const auto value = readExString(f, true);
    return value.empty() ? lib::Array<String>{} : splitLines(value);
}

lib::Array<String> readStrings(ByteIO& f) {
    const auto value = readString(f, true);
    return value.empty() ? lib::Array<String>{} : splitLines(value);
}

String readSound(const RData&, ByteIO& f) {
    switch (f.readUByte()) {
    case 0: return {};
    case 1: return readExString(f);
    case 2: {
        const auto length = static_cast<std::uint32_t>(f.readExUInt());
        if (length == 0) return {};
        return utils::bSndToStr(f.read(length));
    }
    default: throw invalidValue("Unknown sound type", f.bytes()[f.pointer() - 1]);
    }
}

void writeBool(ByteIO& f, bool value) {
    f.writeL(static_cast<Byte>(value ? 1 : 0));
}

void writeStringImpl(ByteIO& f, StringView value, bool lns, bool cutText,
                     const std::function<void(std::uint32_t)>& writeSize) {
    String text = replaceAll(String(value), "\n", "\r\n");
    if (cutText) text.insert(0, "TEXT\r\n");
    if (!text.empty()) {
        String bytes = sjis::tosjis(text);
        if (!lns) bytes.push_back('\0');
        writeSize(checkedLength(bytes.size()));
        f.write(binary::ByteView(reinterpret_cast<const Byte*>(bytes.data()), bytes.size()));
    } else if (lns) {
        writeSize(0);
    } else {
        writeSize(1);
        f.writeL(static_cast<Byte>(0));
    }
}

void writeExString(ByteIO& f, StringView value, bool lns, bool cutText) {
    writeStringImpl(f, value, lns, cutText,
                    [&f](std::uint32_t size) { f.writeExUInt(static_cast<std::int32_t>(size)); });
}

void writeString(ByteIO& f, StringView value, bool lns, bool cutText) {
    writeStringImpl(f, value, lns, cutText,
                    [&f](std::uint32_t size) { f.writeL(size); });
}

void writeExStrings(ByteIO& f, const lib::Array<String>& values) {
    if (values.empty()) {
        writeExString(f, {}, true);
        return;
    }
    String joined;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) joined.push_back('\n');
        joined += values[i];
    }
    if (!joined.empty() && joined.back() != '\n') joined.push_back('\n');
    writeExString(f, joined, true);
}

void writeStrings(ByteIO& f, const lib::Array<String>& values) {
    if (values.empty()) {
        writeString(f, {}, true);
        return;
    }
    String joined;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) joined.push_back('\n');
        joined += values[i];
    }
    if (!joined.empty() && joined.back() != '\n') joined.push_back('\n');
    writeString(f, joined, true);
}

void writeImageImpl(SData& d, ByteIO& f, const lib::Owner<CWXPath>& path,
                    StringView imagePath,
                    const std::function<void(std::uint32_t)>& writeSize) {
    if (imagePath.empty()) {
        writeSize(0);
        return;
    }
    lib::Bytes bytes;
    if (utils::isBinImg(imagePath)) {
        bytes = utils::strToBImg(imagePath);
    } else {
        const auto file = d.skin
            ? fsPath(d.skin->findImagePath(imagePath, d.sPath, ""))
            : fsPath(d.sPath) / fsPath(normalizeHostPath(String(imagePath)));
        // DはサイズやimageRefを公開する前に、既存のパスを読む。
        // 既存ディレクトリの場合も、読み込み失敗を伝播させる必要がある。
        if (std::filesystem::exists(file)) bytes = utils::readBinary(file);
        if (d.saveInnerImagePath && path) {
            // 所有元は拡張データのオブジェクトの場合がある。安定した
            // CWXパスを持たなければ、パス記録は可能な範囲で行う。
            try {
                d.imageRef[path->cwxPath(true)] = encodePathLegacy(String(imagePath));
            } catch (const std::exception&) {
            }
        }
    }
    writeSize(checkedLength(bytes.size()));
    f.write(binary::ByteView(bytes));
}

void writeExImage(SData& d, ByteIO& f, const lib::Owner<CWXPath>& path,
                  StringView imagePath) {
    writeImageImpl(d, f, path, imagePath,
                   [&f](std::uint32_t size) { f.writeExUInt(static_cast<std::int32_t>(size)); });
}

void writeImage(SData& d, ByteIO& f, const lib::Owner<CWXPath>& path,
                StringView imagePath) {
    writeImageImpl(d, f, path, imagePath,
                   [&f](std::uint32_t size) { f.writeL(size); });
}

void SData::merge(const SData& other) {
    for (const auto& [key, value] : other.comment) comment.insert_or_assign(key, value);
    for (const auto& [key, value] : other.imageRef) imageRef.insert_or_assign(key, value);
    for (const auto& [key, value] : other.cardRef) cardRef.insert_or_assign(key, value);
    for (const auto& [key, value] : other.maxNest) maxNest.insert_or_assign(key, value);
    for (const auto& [key, value] : other.nestCount) nestCount.insert_or_assign(key, value);
}

namespace {

template <typename T>
[[nodiscard]] Byte fromEnum(T value, std::initializer_list<T> values,
                            StringView name) {
    std::int32_t index = 0;
    for (const auto candidate : values) {
        if (candidate == value) return static_cast<Byte>(index);
        ++index;
    }
    throw std::invalid_argument(String("Unknown ") + String(name) + " value");
}

}  // namespace

Byte fromTargetT(types::Target value) {
    switch (value.m) {
    case types::Target::M::Selected: return 0;
    case types::Target::M::Random: return 1;
    case types::Target::M::Unselected: return 2;
    default: throw std::invalid_argument("Unknown target T value");
    }
}

Byte fromTargetA(types::Target value) {
    if (value.m == types::Target::M::Unselected) {
        throw std::invalid_argument("Unselected is not valid for target A");
    }
    if (value.sleep) {
        switch (value.m) {
        case types::Target::M::Selected: return 3;
        case types::Target::M::Random: return 4;
        case types::Target::M::Party: return 5;
        default: throw std::invalid_argument("Unknown sleeping target A value");
        }
    }
    switch (value.m) {
    case types::Target::M::Selected: return 0;
    case types::Target::M::Random: return 1;
    case types::Target::M::Party: return 2;
    default: throw std::invalid_argument("Unknown target A value");
    }
}

Byte fromEffectType(types::EffectType value) {
    return fromEnum(value, {types::EffectType::Physic, types::EffectType::Magic,
                            types::EffectType::MagicalPhysic,
                            types::EffectType::PhysicalMagic,
                            types::EffectType::None}, "effect type");
}
Byte fromResist(types::Resist value) {
    return fromEnum(value, {types::Resist::Avoid, types::Resist::Resist,
                            types::Resist::Unfail}, "resist");
}
Byte fromCardVisual(types::CardVisual value) {
    return fromEnum(value, {types::CardVisual::None, types::CardVisual::Reverse,
                            types::CardVisual::Horizontal,
                            types::CardVisual::Vertical}, "card visual");
}
Byte fromRange(types::Range value) {
    switch (value) {
    case types::Range::Selected: return 0;
    case types::Range::Random: return 1;
    case types::Range::Party: return 2;
    case types::Range::Backpack: return 3;
    case types::Range::PartyAndBackpack: return 4;
    case types::Range::Field: return 5;
    case types::Range::CouponHolder:
    case types::Range::CardTarget:
    case types::Range::SelectedCard:
    case types::Range::Npc: return 0;
    }
    throw std::invalid_argument("Unknown range value");
}
Byte fromRangeEffectContent(types::Range value) {
    switch (value) {
    case types::Range::Selected: return 0;
    case types::Range::Random: return 1;
    case types::Range::Party: return 2;
    case types::Range::CardTarget: return 3;
    case types::Range::Backpack:
    case types::Range::PartyAndBackpack:
    case types::Range::Field:
    case types::Range::CouponHolder:
    case types::Range::SelectedCard:
    case types::Range::Npc: return 0;
    }
    throw std::invalid_argument("Unknown effect-content range value");
}
Byte fromKeyCodeRange(types::Range value) {
    switch (value) {
    case types::Range::Selected: return 0;
    case types::Range::Random: return 1;
    case types::Range::Backpack: return 2;
    case types::Range::PartyAndBackpack: return 3;
    case types::Range::CouponHolder:
    case types::Range::SelectedCard: return 0;
    default: throw std::invalid_argument("Unknown key-code range value");
    }
}
Byte fromCouponRange(types::Range value) {
    switch (value) {
    case types::Range::Selected: return 0;
    case types::Range::Random: return 1;
    case types::Range::Party: return 2;
    case types::Range::Field: return 3;
    case types::Range::Npc: return 0;
    default: throw std::invalid_argument("Unknown coupon range value");
    }
}
Byte fromCastRanges(const lib::Array<types::CastRange>& values) {
    Byte result = 0;
    for (const auto value : values) {
        switch (value) {
        case types::CastRange::Party: result |= 0x01; break;
        case types::CastRange::Enemy: result |= 0x02; break;
        case types::CastRange::Npc: result |= 0x04; break;
        default: throw std::invalid_argument("Unknown cast range value");
        }
    }
    return result;
}
Byte fromEffectCardType(types::EffectCardType value) {
    switch (value) {
    case types::EffectCardType::All: return 0;
    case types::EffectCardType::Skill: return 1;
    case types::EffectCardType::Item: return 2;
    case types::EffectCardType::Beast: return 3;
    default: throw std::invalid_argument("Hand is not valid in classic files");
    }
}
Byte fromComparison4(types::Comparison4 value) {
    return fromEnum(value, {types::Comparison4::Eq, types::Comparison4::Ne,
                            types::Comparison4::Lt, types::Comparison4::Gt}, "comparison");
}
Byte fromComparison3(types::Comparison3 value) {
    return fromEnum(value, {types::Comparison3::Eq, types::Comparison3::Lt,
                            types::Comparison3::Gt}, "comparison");
}
Byte fromBorderingType(types::BorderingType value) {
    switch (value) {
    case types::BorderingType::Outline: return 0;
    case types::BorderingType::Inline: return 1;
    default: throw std::invalid_argument("Unknown bordering type");
    }
}
Byte fromBlendMode(types::BlendMode value, bool mask) {
    if (mask) return 1;
    switch (value) {
    case types::BlendMode::Normal: return 0;
    case types::BlendMode::Add: return 2;
    case types::BlendMode::Subtract: return 3;
    case types::BlendMode::Multiply: return 4;
    default: throw std::invalid_argument("Unknown blend mode");
    }
}
Byte fromGradientDir(types::GradientDir value) {
    switch (value) {
    case types::GradientDir::None: return 0;
    case types::GradientDir::LeftToRight: return 1;
    case types::GradientDir::TopToBottom: return 2;
    default: throw std::invalid_argument("Unknown gradient direction");
    }
}
Byte fromCoordinateType(types::CoordinateType value) {
    switch (value) {
    case types::CoordinateType::Absolute: return 0;
    case types::CoordinateType::Relative: return 1;
    case types::CoordinateType::Percentage: return 2;
    default: throw std::invalid_argument("Unknown coordinate type");
    }
}
Byte fromStatus(types::Status value) {
    const auto raw = static_cast<std::int32_t>(value);
    if (raw >= 0 && raw <= 26) return static_cast<Byte>(raw);
    throw std::invalid_argument("Unknown status");
}
Byte fromElement(types::Element value) {
    const auto raw = static_cast<std::int32_t>(value);
    if (raw >= 0 && raw <= 6) return static_cast<Byte>(raw);
    throw std::invalid_argument("Unknown element");
}
Byte fromDamageType(types::DamageType value) {
    switch (value) {
    case types::DamageType::LevelRatio: return 0;
    case types::DamageType::Normal: return 1;
    case types::DamageType::Max: return 2;
    default: throw std::invalid_argument("Fixed damage is not valid in classic files");
    }
}
std::uint32_t fromPhysical(types::Physical value) {
    const auto raw = static_cast<std::int32_t>(value);
    if (raw >= 0 && raw <= 5) return static_cast<std::uint32_t>(raw);
    throw std::invalid_argument("Unknown physical attribute");
}
std::int32_t fromMental(types::Mental value) {
    switch (value) {
    case types::Mental::Aggressive: return 1;
    case types::Mental::Cheerful: return 2;
    case types::Mental::Brave: return 3;
    case types::Mental::Cautious: return 4;
    case types::Mental::Trickish: return 5;
    case types::Mental::Unaggressive: return -1;
    case types::Mental::Uncheerful: return -2;
    case types::Mental::Unbrave: return -3;
    case types::Mental::Uncautious: return -4;
    case types::Mental::Untrickish: return -5;
    }
    throw std::invalid_argument("Unknown mental attribute");
}
Byte fromMentality(types::Mentality value) {
    const auto raw = static_cast<std::int32_t>(value);
    if (raw >= 0 && raw <= 5) return static_cast<Byte>(raw);
    throw std::invalid_argument("Unknown mentality");
}
Byte fromCardTarget(types::CardTarget value) {
    const auto raw = static_cast<std::int32_t>(value);
    if (raw >= 0 && raw <= 4) return static_cast<Byte>(raw);
    throw std::invalid_argument("Unknown card target");
}
Byte fromPremium(types::Premium value) {
    const auto raw = static_cast<std::int32_t>(value);
    if (raw >= 0 && raw <= 2) return static_cast<Byte>(raw);
    throw std::invalid_argument("Unknown card premium");
}

String saveComment(const SData& d) {
    if (d.comment.empty()) return {};
    auto node = xml::XNode::create("comments");
    node.newAttr("dataVersion", 1);
    for (const auto& [key, value] : d.comment) {
        auto child = node.newElement("comment", value);
        child.newAttr("path", key.path);
        if (key.event) child.newAttr("event", true);
    }
    return node.text();
}

String saveImageRef(const SData& d) {
    if (!d.saveInnerImagePath || d.imageRef.empty()) return {};
    auto node = xml::XNode::create("imageRefs");
    node.newAttr("dataVersion", 1);
    std::vector<std::pair<String, String>> entries(d.imageRef.begin(), d.imageRef.end());
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });
    for (const auto& [path, value] : entries) {
        auto child = node.newElement("imageRef", utils::encodePath(value));
        child.newAttr("path", path);
    }
    return node.text();
}

String saveCardRef(const SData& d) {
    if (d.cardRef.empty() && d.maxNest.empty()) return {};
    auto node = xml::XNode::create("cardRefs");
    node.newAttr("dataVersion", 1);
    std::vector<std::pair<String, std::uint32_t>> nests(d.maxNest.begin(), d.maxNest.end());
    std::sort(nests.begin(), nests.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [path, value] : nests) {
        auto child = node.newElement("maxNest", std::to_string(value));
        child.newAttr("path", path);
    }
    std::vector<std::pair<String, std::uint64_t>> refs(d.cardRef.begin(), d.cardRef.end());
    std::sort(refs.begin(), refs.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [path, value] : refs) {
        auto child = node.newElement("cardRef", std::to_string(value));
        child.newAttr("path", path);
    }
    return node.text();
}

OwnerSummary loadSummary(RData& d, ByteIO& f, std::uint64_t& startAreaId) {
    const auto image = readImage(d, f);
    const auto name = readString(f);
    lib::Array<lib::Owner<CardImage>> images;
    if (!image.empty()) {
        images.push_back(std::make_shared<CardImage>(
            image, types::CardImagePosition::Default));
    }
    const auto description = readString(f, true);
    const auto author = readString(f);
    const auto required = readStrings(f);
    const auto requiredNumber = f.readUIntL();
    const auto encodedArea = f.readUIntL();
    if (encodedArea < 19999U) {
        d.dataVersion = 0;
        startAreaId = encodedArea;
    } else if (encodedArea < 39999U) {
        d.dataVersion = 2;
        startAreaId = encodedArea - 20000U;
    } else if (encodedArea < 69999U) {
        d.dataVersion = 4;
        startAreaId = encodedArea - 40000U;
    } else {
        d.dataVersion = 7;
        startAreaId = encodedArea - 70000U;
    }

    auto summary = std::make_shared<Summary>(name, d.skinType, d.skinName,
                                             d.sPath, false, true);
    summary->imagePaths(images);
    summary->desc(description);
    summary->author(author);
    if (d.cardOnly) return summary;
    summary->rCoupons(required);
    summary->rCouponNum(requiredNumber);

    const auto root = summary->flagDirRoot();
    if (!root) throw std::runtime_error("Summary has no flag root");
    const auto flagsParent = [&root](StringView path) {
        auto dir = root;
        const auto parentPath = flag::FlagDir::up(String(path));
        if (!parentPath.empty()) {
            std::size_t begin = 0;
            while (begin <= parentPath.size()) {
                const auto end = parentPath.find('\\', begin);
                const auto part = parentPath.substr(
                    begin, end == String::npos ? parentPath.size() - begin
                                                : end - begin);
                if (!part.empty()) {
                    auto sub = dir->getSubDir(part);
                    if (!sub) {
                        sub = std::make_shared<flag::FlagDir>(String(part));
                        if (!dir->add(sub)) {
                            throw std::runtime_error("Invalid flag directory: " +
                                                     String(path));
                        }
                    }
                    dir = std::move(sub);
                }
                if (end == String::npos) break;
                begin = end + 1;
            }
        }
        return dir;
    };

    if (d.dataVersion < 7) {
        const auto stepCount = f.readUIntL();
        for (std::uint32_t i = 0; i < stepCount; ++i) {
            const auto path = readString(f);
            auto selected = f.readUIntL();
            lib::Array<String> values;
            values.reserve(10);
            for (std::uint32_t j = 0; j < 10; ++j) values.push_back(readString(f));
            if (values.empty()) values.push_back({});
            selected = std::min<std::uint32_t>(selected,
                                                static_cast<std::uint32_t>(values.size() - 1));
            auto step = std::make_shared<flag::Step>(
                flag::FlagDir::basename(path), values, selected);
            if (!flagsParent(path)->add(step)) {
                throw std::runtime_error("Invalid step path: " + path);
            }
        }
        const auto flagCount = f.readUIntL();
        for (std::uint32_t i = 0; i < flagCount; ++i) {
            const auto path = readString(f);
            auto flagValue = readBool(f);
            auto on = readString(f);
            auto off = readString(f);
            auto value = std::make_shared<flag::Flag>(
                flag::FlagDir::basename(path), on, off, flagValue);
            if (!flagsParent(path)->add(value)) {
                throw std::runtime_error("Invalid flag path: " + path);
            }
        }
        (void)f.readUIntL();
        if (d.dataVersion != 0) {
            summary->levelMin(f.readUIntL());
            summary->levelMax(f.readUIntL());
        }
    } else {
        for (std::size_t i = 0; i < 20; ++i) (void)f.readByte();
        const auto stepCount = static_cast<std::uint32_t>(f.readExUInt());
        for (std::uint32_t i = 0; i < stepCount; ++i) {
            const auto path = readExString(f);
            auto type = f.readUByte();
            const bool expand = (type & 0x80U) != 0;
            type &= 0x7FU;
            if (expand) (void)f.readUByte();
            const auto valueCount = static_cast<std::uint32_t>(f.readExUInt());
            std::shared_ptr<flag::Step> step;
            if (type == 1) {
                lib::Array<String> values;
                values.reserve(valueCount == 0 ? 1 : valueCount);
                for (std::uint32_t j = 0; j < valueCount; ++j) values.push_back(readExString(f));
                auto selected = static_cast<std::uint32_t>(f.readExUInt());
                // Dは空の値一覧でcount-1がアンダーフローし、Step.setValues
                // の範囲アサートに違反する。この不正な旧形式入力には、
                // 無効なStepではなく使用可能な仮の値を保持する。
                if (values.empty()) values.push_back({});
                selected = std::min<std::uint32_t>(selected,
                                                    static_cast<std::uint32_t>(values.size() - 1));
                step = std::make_shared<flag::Step>(flag::FlagDir::basename(path), values,
                                                    selected);
                step->expandSPChars(expand);
                if (!flagsParent(path)->add(step)) {
                    throw std::runtime_error("Invalid step path: " + path);
                }
            } else if (type == 2) {
                const auto selected = static_cast<std::uint32_t>(f.readExUInt());
                if (valueCount <= d.numberStepToVariantThreshold) {
                    lib::Array<String> values;
                    values.reserve(valueCount == 0 ? 1 : valueCount);
                    for (std::uint32_t j = 0; j < valueCount; ++j) {
                        values.push_back(std::to_string(j));
                    }
                    // 上記の文字列の値一覧と同様に、空の数値一覧でもDのcount-1
                    // のアンダーフローや無効なStepを再現しない。
                    if (values.empty()) values.push_back({});
                    auto clamped = std::min<std::uint32_t>(
                        selected, static_cast<std::uint32_t>(values.size() - 1));
                    step = std::make_shared<flag::Step>(
                        flag::FlagDir::basename(path), values, clamped);
                    if (!flagsParent(path)->add(step)) {
                        throw std::runtime_error("Invalid step path: " + path);
                    }
                } else {
                    auto variant = std::make_shared<flag::Variant>(
                        flag::FlagDir::basename(path), static_cast<double>(selected));
                    if (!flagsParent(path)->add(variant)) {
                        throw std::runtime_error("Invalid variant path: " + path);
                    }
                    d.numberSteps[path] = valueCount;
                }
            } else {
                throw std::runtime_error("Invalid classic step type");
            }
            d.stepNames[path] = true;
        }
        for (const auto& step : root->allSteps()) {
            if (!step || !step->expandSPChars()) continue;
            for (std::uint32_t i = 0; i < step->count(); ++i) {
                auto value = step->getValue(i);
                for (const auto& [path, unused] : d.numberSteps) {
                    (void)unused;
                    value = msgutils::replStepToVariantInText(value, path, path);
                }
                step->setValue(i, std::move(value));
            }
        }
        const auto flagCount = static_cast<std::uint32_t>(f.readExUInt());
        for (std::uint32_t i = 0; i < flagCount; ++i) {
            const auto path = readExString(f);
            const auto flagValue = readBool(f);
            auto on = readExString(f);
            auto off = readExString(f);
            auto value = std::make_shared<flag::Flag>(
                flag::FlagDir::basename(path), on, off, flagValue);
            if (!flagsParent(path)->add(value)) {
                throw std::runtime_error("Invalid flag path: " + path);
            }
        }
        summary->levelMin(f.readExUInt());
        summary->levelMax(f.readExUInt());
    }
    return summary;
}

OwnerMotion readMotion(RData& d, std::int32_t dataVersion, ByteIO& f,
                       std::size_t) {
    const auto tableType = f.readUByte();
    if (dataVersion > 2) for (int i = 0; i < 5; ++i) (void)f.readByte();
    const auto element = toElement(f.readUByte());
    const auto type = tableType == 8 ? Byte{0} : f.readUByte();
    using motion::MType;
    auto make = [element](MType t) {
        return std::make_shared<Motion>(t, element);
    };
    if (tableType == 0 || tableType == 1) {
        const auto damageType = toDamageType(f.readUByte());
        const auto value = f.readUIntL();
        MType motionType{};
        if (tableType == 0) {
            switch (type) {
            case 0: motionType = MType::Heal; break;
            case 1: motionType = MType::Damage; break;
            case 2: motionType = MType::Absorb; break;
            default: throw std::runtime_error("Unknown classic recovery motion");
            }
        } else {
            switch (type) {
            case 0: motionType = MType::Paralyze; break;
            case 1: motionType = MType::DisParalyze; break;
            case 2: motionType = MType::Poison; break;
            case 3: motionType = MType::DisPoison; break;
            default: throw std::runtime_error("Unknown classic status motion");
            }
        }
        auto motion = make(motionType);
        motion->damageType(damageType);
        motion->uValue(static_cast<std::int32_t>(value));
        return motion;
    }
    if (tableType == 2) {
        if (type == 0) return make(MType::GetSkillPower);
        if (type == 1) return make(MType::LoseSkillPower);
        throw std::runtime_error("Unknown classic skill-power motion");
    }
    if (tableType == 3 || tableType == 4) {
        const auto rounds = dataVersion > 2 ? f.readUIntL() : 10U;
        MType motionType{};
        if (tableType == 3) {
            constexpr std::array<MType, 6> values{
                MType::Sleep, MType::Confuse, MType::Overheat,
                MType::Brave, MType::Panic, MType::Normal};
            if (type >= values.size()) throw std::runtime_error("Unknown classic mentality motion");
            motionType = values[type];
        } else {
            constexpr std::array<MType, 8> values{
                MType::Bind, MType::DisBind, MType::Silence, MType::DisSilence,
                MType::FaceUp, MType::FaceDown, MType::AntiMagic, MType::DisAntiMagic};
            if (type >= values.size()) throw std::runtime_error("Unknown classic status motion");
            motionType = values[type];
        }
        auto motion = make(motionType);
        if (motion->detail().use(motion::MArg::Round)) {
            motion->round(static_cast<std::int32_t>(rounds));
        }
        return motion;
    }
    if (tableType == 5) {
        const auto value = f.readUIntL();
        const auto rounds = dataVersion > 2 ? f.readUIntL() : 10U;
        constexpr std::array<MType, 4> values{
            MType::EnhanceAction, MType::EnhanceAvoid,
            MType::EnhanceResist, MType::EnhanceDefense};
        if (type >= values.size()) throw std::runtime_error("Unknown classic enhancement motion");
        auto motion = make(values[type]);
        motion->round(static_cast<std::int32_t>(rounds));
        motion->aValue(static_cast<std::int32_t>(value));
        return motion;
    }
    if (tableType == 6) {
        constexpr std::array<MType, 3> values{
            MType::VanishTarget, MType::VanishCard, MType::VanishBeast};
        if (type >= values.size()) throw std::runtime_error("Unknown classic vanish motion");
        return make(values[type]);
    }
    if (tableType == 7) {
        constexpr std::array<MType, 9> values{
            MType::DealAttackCard, MType::DealPowerfulAttackCard,
            MType::DealCriticalAttackCard, MType::DealFeintCard,
            MType::DealDefenseCard, MType::DealDistanceCard,
            MType::DealConfuseCard, MType::DealSkillCard, MType::CancelAction};
        if (type >= values.size()) throw std::runtime_error("Unknown classic card motion");
        return make(values[type]);
    }
    if (tableType == 8) {
        lib::Owner<BeastCard> beast;
        const auto count = f.readUIntL();
        for (std::uint32_t i = 0; i < count; ++i) {
            auto copy = d;
            beast = loadBeast(copy, f, 1);
        }
        auto motion = make(MType::SummonBeast);
        motion->newBeast(beast);
        return motion;
    }
    throw std::runtime_error("Unknown classic motion table type");
}

OwnerBgImage readBgImage(const RData& d, ByteIO& f, bool, std::size_t) {
    const auto x = f.readIntL();
    const auto y = f.readIntL();
    std::uint32_t version = 0;
    const auto width = readCellWidth(f, version);
    const auto height = f.readUIntL();
    if (version <= 4) {
        const auto path = decodePathLegacy(readString(const_cast<ByteIO&>(f)));
        const auto mask = readBool(const_cast<ByteIO&>(f));
        if (version <= 2) {
            return std::make_shared<background::ImageCell>(path, String{}, x, y,
                                                           static_cast<std::int32_t>(width),
                                                           static_cast<std::int32_t>(height), mask);
        }
        const auto flag = readString(const_cast<ByteIO&>(f));
        (void)f.readByte();
        return std::make_shared<background::ImageCell>(path, flag, x, y,
                                                       static_cast<std::int32_t>(width),
                                                       static_cast<std::int32_t>(height), mask);
    }
    const auto type = f.readUByte();
    if (type == 0) {
        const auto mask = readBool(const_cast<ByteIO&>(f));
        const auto foreground = readBool(const_cast<ByteIO&>(f));
        const auto included = readBool(const_cast<ByteIO&>(f));
        const auto path = included
                              ? readExImage(d, const_cast<ByteIO&>(f))
                              : (version >= 7
                                                     ? readExString(const_cast<ByteIO&>(f))
                                                     : readString(const_cast<ByteIO&>(f)));
        const auto flag = version >= 7 ? readExString(const_cast<ByteIO&>(f))
                                       : readString(const_cast<ByteIO&>(f));
        (void)f.readByte();
        const auto cellName = readExString(f);
        auto cell = std::make_shared<background::ImageCell>(
            path, flag, x, y, static_cast<std::int32_t>(width),
            static_cast<std::int32_t>(height), mask);
        setCellCommon(cell, foreground, cellName);
        return cell;
    }
    if (type == 2) {
        const auto mask = readBool(const_cast<ByteIO&>(f));
        const auto foreground = version >= 7 ? readBool(const_cast<ByteIO&>(f)) : false;
        auto text = version >= 7 ? readExString(const_cast<ByteIO&>(f))
                                 : readString(const_cast<ByteIO&>(f));
        text = replaceAll(std::move(text), "\r", "");
        // Dの不具合を意図的に修正する。数値ステップはloadSummaryです
        // でにコモンへ変換されているため、文字列中の参照も移行する必要がある。
        for (const auto& [path, unused] : d.numberSteps) {
            (void)unused;
            text = msgutils::replStepToVariantInText(text, path, path);
        }
        const auto fontName = version >= 7 ? readExString(const_cast<ByteIO&>(f))
                                           : readString(const_cast<ByteIO&>(f));
        const auto size = version >= 7 ? f.readExUInt() : f.readUIntL();
        const auto color = readRgb(const_cast<ByteIO&>(f));
        const auto style = f.readUByte();
        const bool bold = (style & 0x01U) != 0;
        const bool italic = (style & 0x02U) != 0;
        const bool underline = (style & 0x04U) != 0;
        const bool strike = (style & 0x08U) != 0;
        const bool bordering = (style & 0x10U) != 0;
        const bool vertical = (style & 0x20U) != 0;
        auto borderingType = types::BorderingType::None;
        auto borderingColor = structs::CRGB{255, 255, 255, 255};
        std::uint32_t borderingWidth = 1;
        if (bordering) {
            borderingType = toBorderingType(f.readUByte());
            borderingColor = readRgb(const_cast<ByteIO&>(f));
            borderingWidth = version >= 7 ? f.readExUInt() : f.readUIntL();
        }
        if (version >= 7) {
            for (int i = 0; i < 5; ++i) (void)f.readByte();
        } else {
            (void)f.readByte();
            (void)f.readUIntL();
            (void)f.readUIntL();
            (void)f.readByte();
        }
        const auto flag = version >= 7 ? readExString(const_cast<ByteIO&>(f))
                                       : readString(const_cast<ByteIO&>(f));
        (void)f.readByte();
        const auto cellName = version >= 7 ? readExString(f) : String{};
        auto cell = std::make_shared<background::TextCell>(
            text, fontName, size, color, bold, italic, underline, strike, vertical,
            false, borderingType, borderingColor, borderingWidth,
            types::UpdateType::Fixed, flag, x, y, static_cast<std::int32_t>(width),
            static_cast<std::int32_t>(height), mask);
        setCellCommon(cell, foreground, cellName);
        return cell;
    }
    if (type == 3) {
        bool mask = false;
        const auto blend = toBlendMode(f.readUByte(), mask);
        const auto foreground = version >= 7 ? readBool(const_cast<ByteIO&>(f)) : false;
        const auto gradient = toGradientDir(f.readUByte());
        const auto color1 = readBgr(f);
        const auto color2 = gradient == types::GradientDir::None
                                ? color1
                                : readBgr(f);
        const auto flag = version >= 7 ? readExString(const_cast<ByteIO&>(f))
                                       : readString(const_cast<ByteIO&>(f));
        (void)f.readByte();
        const auto cellName = version >= 7 ? readExString(f) : String{};
        auto cell = std::make_shared<background::ColorCell>(
            blend, gradient, color1, color2, flag, x, y,
            static_cast<std::int32_t>(width), static_cast<std::int32_t>(height), mask);
        setCellCommon(cell, foreground, cellName);
        return cell;
    }
    if (type == 4) {
        const auto mask = readBool(const_cast<ByteIO&>(f));
        const auto foreground = version >= 7 ? readBool(const_cast<ByteIO&>(f)) : false;
        const auto pc = f.readUByte();
        const auto flag = version >= 7 ? readExString(const_cast<ByteIO&>(f))
                                       : readString(const_cast<ByteIO&>(f));
        (void)f.readByte();
        const auto cellName = version >= 7 ? readExString(f) : String{};
        auto cell = std::make_shared<background::PCCell>(
            pc, false, flag, x, y, static_cast<std::int32_t>(width),
            static_cast<std::int32_t>(height), mask);
        setCellCommon(cell, foreground, cellName);
        return cell;
    }
    throw std::runtime_error("Unknown classic background cell type");
}

lib::Array<OwnerBgImage> readBgImages(const RData& d, ByteIO& f,
                                       std::int32_t dataVersion, bool area,
                                       bool replaceBackground) {
    const auto count = readClassicCount(f, dataVersion);
    lib::Array<OwnerBgImage> images;
    images.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) images.push_back(readBgImage(d, f, area, i));
    if (replaceBackground || images.empty()) return images;
    if (const auto image = std::dynamic_pointer_cast<background::ImageCell>(images.front());
        image && image->path().empty() && image->flag().empty() && image->x() == 0 &&
        image->y() == 0 && image->width() == 632 && image->height() == 420 &&
        !image->mask() && image->cellName().empty() &&
        image->layer() == background::BgImage::LAYER_BACK_CELL) {
        images.erase(images.begin());
    }
    return convInheritCellsR(images);
}

lib::Array<OwnerBgImage> convInheritCellsR(const lib::Array<OwnerBgImage>& input) {
    auto cells = input;
    if (!cells.empty()) {
        const auto image = std::dynamic_pointer_cast<background::ImageCell>(cells.front());
        if (image && image->flag().empty() && image->x() == 0 && image->y() == 0 &&
            image->width() == 632 && image->height() == 420 && !image->mask() &&
            image->cellName().empty() && image->layer() != background::BgImage::LAYER_BACK_CELL) {
            cells.insert(cells.begin(), std::make_shared<background::ColorCell>(
                types::BlendMode::Normal, types::GradientDir::None,
                structs::CRGB{0, 0, 0, 255}, structs::CRGB{0, 0, 0, 255},
                String{}, 0, 0, 0, 0, false));
        }
    }
    return cells;
}

void readAreaHeader(RData& d, ByteIO& f, std::uint64_t& id, String& name) {
    const auto tag = f.readUByte();
    if (tag == 0xFFU) {
        d.dataVersion = f.readUByte();
        name = readExString(f);
        id = f.readExUInt();
        return;
    }
    const auto second = f.readByte();
    if (second == 'B') {
        (void)f.read(69);
        name = readString(f);
        const auto encoded = f.readUIntL();
        if (encoded < 19999U) {
            d.dataVersion = 0;
            id = encoded;
        } else {
            d.dataVersion = 2;
            id = encoded - 20000U;
        }
        return;
    }
    d.dataVersion = 4;
    (void)f.readByte();
    (void)f.readByte();
    (void)f.readByte();
    name = readString(f);
    id = f.readUIntL() - 40000U;
}

OwnerContent readContent(RData& d, ByteIO& f, std::size_t index) {
    struct Header {
        Byte type{};
        std::int32_t version{};
        String name;
        lib::Dictionary<String, String> info;
    };

    std::function<OwnerContent()> readNode;
    readNode = [&]() -> OwnerContent {
        lib::Array<Header> headers;
        lib::Array<OwnerContent> children;
        while (true) {
            Header header;
            const auto headerOffset = f.pointer();
            std::uint32_t childCountRaw{};
            try {
                header.type = f.readUByte();
                header.name = readString(f, header.info);
                childCountRaw = f.readUIntL();
            } catch (const std::exception& error) {
                std::ostringstream message;
                message << "classic content header=0x" << std::hex << headerOffset
                        << " chain=";
                for (const auto& previous : headers)
                    message << std::dec << static_cast<unsigned>(previous.type) << ',';
                message << ": " << error.what();
                throw std::runtime_error(message.str());
            }
            if (childCountRaw <= 19999U) {
                header.version = 0;
            } else if (childCountRaw <= 39999U) {
                header.version = 2;
                childCountRaw -= 20000U;
            } else if (childCountRaw <= 69999U) {
                header.version = 4;
                childCountRaw -= 40000U;
            } else {
                header.version = 7;
                childCountRaw -= 70000U;
            }
            headers.push_back(std::move(header));
            if (childCountRaw == 0U) break;
            if (childCountRaw == 1U) continue;
            children.reserve(childCountRaw);
            for (std::uint32_t i = 0; i < childCountRaw; ++i)
                children.push_back(readNode());
            break;
        }

        OwnerContent result;
        lib::Dictionary<String, String> branchMultiStep;
        lib::Dictionary<String, std::pair<String, String>> branchStepCompare;
        for (auto header = headers.rbegin(); header != headers.rend(); ++header) {
        const auto type = header->type;
        const auto version = header->version;
        const auto& name = header->name;
        const auto& info = header->info;
        const auto text = [&]() { return readClassicText(f, version); };
        const auto count = [&]() { return readClassicCount(f, version); };
        const auto id = [&]() { return readClassicId(f, version); };
        const auto signedValue = [&]() { return readClassicSigned(f, version); };
        // Dと同様に、拡張タグ74と77はヘッダーバージョンに関係なくExIntを使う。
        const auto extendedSignedValue = [&]() { return f.readExInt(); };
        auto make = [&](types::CType contentType) {
            return std::make_shared<Content>(contentType, name);
        };
        const auto bodyOffset = f.pointer();
        try {
        switch (type) {
        case 0:
            result = make(types::CType::Start);
            break;
        case 1:
            result = make(types::CType::LinkStart);
            result->start(text());
            break;
        case 2:
            result = make(types::CType::StartBattle);
            result->battle(id());
            break;
        case 3:
            result = make(types::CType::End);
            result->complete(readBool(f));
            break;
        case 4:
            result = make(types::CType::EndBadEnd);
            break;
        case 5:
            result = make(types::CType::ChangeArea);
            result->area(id());
            result->transition(types::Transition::Default);
            result->transitionSpeed(5);
            break;
        case 6: {
            String messagePath;
            Byte flags = 0;
            if (version < 7) messagePath = readString(f);
            else {
                flags = f.readUByte();
                if (flags & 0x01U) messagePath = readExString(f);
            }
            result = make(types::CType::TalkMessage);
            auto body = text();
            for (const auto& [path, unused] : d.numberSteps) {
                (void)unused;
                body = msgutils::replStepToVariantInText(body, path, path);
            }
            result->text(std::move(body));
            if (messagePath == "??Selected" || messagePath == "??Unselected" ||
                messagePath == "??Random" || messagePath == "??Card") {
                result->cardPaths({messagePath});
            } else if (!messagePath.empty()) {
                // Dは特殊指定以外の各トークンから、File型のCardImage
                // を作る。文字列表現からの変換では、このCWL項目で特殊扱
                // いされないPC#の別名や話者の接尾辞も認識してしまう。
                result->cardImages({std::make_shared<card::CardImage>(
                    decodePathLegacy(messagePath), types::CardImagePosition::Default)});
            }
            if (version >= 7 && (flags & 0x80U)) {
                result->selectionColumns(f.readExUInt());
            }
            break;
        }
        case 7:
            result = make(types::CType::PlayBgm);
            result->bgmPath(decodePathLegacy(text()));
            break;
        case 8: {
            const auto cells = readBgImages(d, f, version, false);
            result = make(types::CType::ChangeBgImage);
            result->backgroundImages(cells);
            result->transition(types::Transition::Default);
            result->transitionSpeed(5);
            break;
        }
        case 9:
            result = make(types::CType::PlaySound);
            result->soundPath(decodePathLegacy(text()));
            break;
        case 10:
            result = make(types::CType::Wait);
            if (version < 7) result->wait(f.readUIntL());
            else {
                const auto milliseconds = f.readExInt();
                const auto wait = milliseconds < 0
                    ? static_cast<int>(std::lround(-milliseconds / 100.0))
                    : milliseconds;
                result->wait(wait);
                if (milliseconds < 0 && wait * 100 != -milliseconds) {
                    const auto detail = utils::tryFormat(
                        static_cast<String>(d.prop->requiredMsgs().convertFromNextWarningWaitMillis),
                        std::to_string(-milliseconds));
                    result->comment(utils::tryFormat(
                        static_cast<String>(d.prop->requiredMsgs().convertFromNextWarning), detail));
                }
            }
            break;
        case 11: {
            result = make(types::CType::Effect);
            result->signedLevel(signedValue());
            result->range(toRangeEffectContent(f.readUByte()));
            result->effectType(toEffectType(f.readUByte()));
            result->resist(toResist(f.readUByte()));
            result->successRate(signedValue());
            auto sound = version < 7 ? readString(f) : readSound(d, f);
            if (sound == "（なし）" || sound == "（なし）.wav") sound.clear();
            result->soundPath(decodePathLegacy(sound));
            result->cardVisual(toCardVisual(f.readUByte()));
            const auto motionCount = count();
            lib::Array<OwnerMotion> motions;
            motions.reserve(motionCount);
            for (std::uint32_t i = 0; i < motionCount; ++i) {
                motions.push_back(readMotion(d, version, f, i));
            }
            if (version >= 7 && std::any_of(motions.begin(), motions.end(),
                    [](const OwnerMotion& motion) {
                        if (!motion) return false;
                        switch (motion->type()) {
                        case types::MType::Damage:
                        case types::MType::Absorb:
                        case types::MType::Paralyze:
                        case types::MType::VanishTarget:
                            return true;
                        default:
                            return false;
                        }
                    })) {
                result->ignite(true);
            }
            result->motions(std::move(motions));
            break;
        }
        case 12:
            result = make(types::CType::BranchSelect);
            result->targetAll(readBool(f));
            result->selectionMethod(readBool(f) ? types::SelectionMethod::Random
                                                 : types::SelectionMethod::Manual);
            break;
        case 13:
            result = make(types::CType::BranchAbility);
            result->signedLevel(signedValue());
            result->targetS(toTargetA(f.readUByte()));
            result->physical(toPhysical(static_cast<std::uint32_t>(signedValue())));
            result->mental(toMental(signedValue()));
            break;
        case 14:
            result = make(types::CType::BranchRandom);
            result->percent(static_cast<int>(count()));
            break;
        case 15:
            result = make(types::CType::BranchFlag);
            result->flag(text());
            break;
        case 16:
            result = make(types::CType::SetFlag);
            result->flag(text());
            result->flagValue(readBool(f));
            break;
        case 17: {
            const auto step = text();
            if (d.numberSteps.contains(step)) {
                result = make(types::CType::TalkMessage);
                result->comment(d.prop->requiredMsgs().contentName(
                                    types::CType::BranchMultiStep) +
                                ": " + step);
                branchMultiStep[result->eventId()] = step;
            } else {
                result = make(types::CType::BranchMultiStep);
                result->step(step);
            }
            break;
        }
        case 18: {
            const auto step = text();
            // DはWsn.7でExIntとして符号化される場合も、
            // 結果をuintへ格納する。通常のステップだけでなく、
            // 不正な値や旧形式の負値にもこの変換契約を維持する。
            const auto value = version < 7
                ? f.readUIntL()
                : static_cast<std::uint32_t>(f.readExInt());
            if (d.numberSteps.contains(step)) {
                result = make(types::CType::SetVariant);
                result->variant(step);
                result->expression(std::to_string(value));
            } else {
                result = make(types::CType::SetStep);
                result->step(step);
                result->stepValue(static_cast<std::int32_t>(value));
            }
            break;
        }
        case 19:
            result = make(types::CType::BranchCast);
            result->casts(id());
            break;
        case 20: case 21: case 23: {
            const auto cardId = id();
            types::CType contentType = type == 20 ? types::CType::BranchItem
                                       : type == 21 ? types::CType::BranchSkill
                                                     : types::CType::BranchBeast;
            result = make(contentType);
            if (type == 20) result->item(cardId);
            else if (type == 21) result->skill(cardId);
            else result->beast(cardId);
            if (version > 2) {
                result->cardNumber(static_cast<int>(count()));
                result->range(toRange(d, f.readUByte(), *result, version));
            } else {
                result->cardNumber(1);
                result->range(types::Range::PartyAndBackpack);
            }
            break;
        }
        case 22:
            result = make(types::CType::BranchInfo);
            result->info(id());
            break;
        case 24:
            result = make(types::CType::BranchMoney);
            result->money(static_cast<int>(count()));
            break;
        case 25:
            result = make(types::CType::BranchCoupon);
            result->couponNames({text()});
            (void)signedValue();
            result->range(toCouponRange(f.readUByte()));
            if (version >= 7) result->invertResult(readBool(f));
            break;
        case 26:
            result = make(types::CType::GetCast);
            result->casts(id());
            break;
        case 27: case 28: case 30: {
            const auto cardId = id();
            const auto contentType = type == 27 ? types::CType::GetItem
                                     : type == 28 ? types::CType::GetSkill
                                                   : types::CType::GetBeast;
            result = make(contentType);
            if (type == 27) result->item(cardId);
            else if (type == 28) result->skill(cardId);
            else result->beast(cardId);
            if (version > 2) {
                result->cardNumber(static_cast<int>(count()));
                result->range(toRange(d, f.readUByte(), *result, version));
            } else {
                result->cardNumber(1);
                result->range(types::Range::PartyAndBackpack);
            }
            break;
        }
        case 29:
            result = make(types::CType::GetInfo);
            result->info(id());
            break;
        case 31:
            result = make(types::CType::GetMoney);
            result->money(static_cast<int>(count()));
            break;
        case 32:
            result = make(types::CType::GetCoupon);
            result->coupon(text());
            result->couponValue(signedValue());
            result->range(toRange(d, f.readUByte(), *result, version));
            if (version >= 7) (void)f.readByte();
            break;
        case 33:
            result = make(types::CType::LoseCast);
            result->casts(id());
            break;
        case 34: case 35: case 37: {
            const auto cardId = id();
            const auto contentType = type == 34 ? types::CType::LoseItem
                                     : type == 35 ? types::CType::LoseSkill
                                                   : types::CType::LoseBeast;
            result = make(contentType);
            if (type == 34) result->item(cardId);
            else if (type == 35) result->skill(cardId);
            else result->beast(cardId);
            if (version > 2) {
                result->cardNumber(static_cast<int>(count()));
                result->range(toRange(d, f.readUByte(), *result, version));
            } else {
                result->cardNumber(1);
                result->range(types::Range::PartyAndBackpack);
            }
            break;
        }
        case 36:
            result = make(types::CType::LoseInfo);
            result->info(id());
            break;
        case 38:
            result = make(types::CType::LoseMoney);
            result->money(static_cast<int>(count()));
            break;
        case 39:
            result = make(types::CType::LoseCoupon);
            result->coupon(text());
            (void)signedValue();
            result->range(toRange(d, f.readUByte(), *result, version));
            if (version >= 7) (void)f.readByte();
            break;
        case 40: {
            result = make(types::CType::TalkDialog);
            const auto target = f.readUByte();
            result->talkerNC(target == 3 ? types::Talker::Valued
                                         : toTargetT(target).m == types::Target::M::Random
                                               ? types::Talker::Random
                                               : toTargetT(target).m == types::Target::M::Unselected
                                                     ? types::Talker::Unselected
                                                     : types::Talker::Selected);
            lib::Array<lib::Owner<coupon::Coupon>> coupons;
            int initial = 0;
            if (target == 3) {
                const auto couponCount = count();
                for (std::uint32_t i = 0; i < couponCount; ++i) {
                    const auto coupon = text();
                    const auto value = signedValue();
                    if (i == 0 && coupon.empty()) initial = value;
                    else coupons.push_back(std::make_shared<coupon::Coupon>(coupon, value));
                }
            }
            result->coupons(std::move(coupons));
            result->initValue(initial);
            const auto dialogCount = count();
            lib::Array<lib::Owner<event::SDialog>> dialogs;
            for (std::uint32_t i = 0; i < dialogCount; ++i) {
                auto required = version < 7 ? readStrings(f) : readExStrings(f);
                auto dialogText = version < 7 ? readString(f, true) : readExString(f, true);
                if (version >= 7) {
                    // DはWSN.7の台詞だけで、数値ステップの置換記号を変換する。
                    for (const auto& [path, unused] : d.numberSteps) {
                        (void)unused;
                        dialogText = msgutils::replStepToVariantInText(dialogText, path, path);
                    }
                }
                dialogs.push_back(std::make_shared<event::SDialog>(
                    std::move(dialogText), std::move(required)));
            }
            result->dialogs(std::move(dialogs));
            if (version >= 7) {
                const auto flags = f.readUByte();
                result->selectTalker((flags & 0x01U) != 0);
                if (flags & 0x02U) result->selectionColumns(f.readUByte());
            }
            break;
        }
        case 41: case 42: {
            const auto step = text();
            if (d.numberSteps.contains(step)) {
                result = make(types::CType::SetVariant);
                result->variant(step);
                const auto escaped = replaceAll(step, "\"", "\"\"");
                result->expression(type == 41
                    ? "MIN(@\"" + escaped + "\" + 1, " + std::to_string(d.numberSteps.at(step) - 1) + ")"
                    : "MAX(@\"" + escaped + "\" - 1, 0)");
            } else {
                result = make(type == 41 ? types::CType::SetStepUp
                                          : types::CType::SetStepDown);
                result->step(step);
            }
            break;
        }
        case 43:
            result = make(types::CType::ReverseFlag);
            result->flag(text());
            break;
        case 44: {
            const auto step = text();
            const auto value = version < 7
                ? f.readUIntL()
                : static_cast<std::uint32_t>(f.readExInt());
            if (d.numberSteps.contains(step)) {
                result = make(types::CType::BranchVariant);
                result->expression(std::to_string(value) + " <= @\"" + replaceAll(step, "\"", "\"\"") + "\"");
            } else {
                result = make(types::CType::BranchStep);
                result->step(step);
                result->stepValue(static_cast<std::int32_t>(value));
            }
            break;
        }
        case 45:
            result = make(types::CType::ElapseTime);
            break;
        case 46:
            result = make(types::CType::BranchLevel);
            result->average(readBool(f));
            result->unsignedLevel(static_cast<int>(count()));
            break;
        case 47:
            result = make(types::CType::BranchStatus);
            result->status(toStatus(f.readUByte()));
            result->range(toRangeE(f.readUByte()));
            break;
        case 48:
            result = make(types::CType::BranchPartyNumber);
            result->partyNumber(static_cast<int>(count()));
            break;
        case 49: result = make(types::CType::ShowParty); break;
        case 50: result = make(types::CType::HideParty); break;
        case 51: result = make(types::CType::EffectBreak); break;
        case 52:
            result = make(types::CType::CallStart);
            result->start(text());
            break;
        case 53:
            result = make(types::CType::LinkPackage);
            result->packages(id());
            break;
        case 54:
            result = make(types::CType::CallPackage);
            result->packages(id());
            break;
        case 55: result = make(types::CType::BranchArea); break;
        case 56: result = make(types::CType::BranchBattle); break;
        case 57: case 58: case 59: {
            const auto ct = type == 57 ? types::CType::BranchCompleteStamp
                         : type == 58 ? types::CType::GetCompleteStamp
                                      : types::CType::LoseCompleteStamp;
            result = make(ct);
            result->completeStamp(text());
            break;
        }
        case 60: case 61: case 62: {
            const auto ct = type == 60 ? types::CType::BranchGossip
                         : type == 61 ? types::CType::GetGossip
                                      : types::CType::LoseGossip;
            result = make(ct);
            result->gossip(text());
            break;
        }
        case 63: result = make(types::CType::BranchIsBattle); break;
        case 64:
            result = make(types::CType::Redisplay);
            result->transition(types::Transition::Default);
            result->transitionSpeed(5);
            break;
        case 65:
            result = make(types::CType::CheckFlag);
            result->flag(text());
            break;
        case 66: {
            const auto step1 = text();
            const auto step2 = text();
            const auto number1 = d.numberSteps.find(step1);
            const auto number2 = d.numberSteps.find(step2);
            if (number1 != d.numberSteps.end() || number2 != d.numberSteps.end()) {
                result = make(types::CType::SetVariant);
                if (number2 != d.numberSteps.end()) result->variant(step2);
                else result->step(step2);
                const auto escaped = replaceAll(step1, "\"", "\"\"");
                if (number1 != d.numberSteps.end()) {
                    result->expression("@\"" + escaped + "\"");
                } else if (d.prop && d.stepNames.find(step1) == d.stepNames.end() &&
                           asciiIEqual(step1, d.prop->sys().randomValue())) {
                    result->expression("DICE(1, " + std::to_string(number2->second) + ") - 1");
                } else if (d.prop && d.stepNames.find(step1) == d.stepNames.end() &&
                           asciiIEqual(step1, d.prop->sys().selectedPlayerCardNumber())) {
                    result->expression("IF(CASTTYPE(SELECTED()) = 1, SELECTED(), 0)");
                } else {
                    result->expression("STEPVALUE(\"" + escaped + "\")");
                }
            } else {
                result = make(types::CType::SubstituteStep);
                result->step(step1);
                result->step2(step2);
            }
            break;
        }
        case 67:
            result = make(types::CType::SubstituteFlag);
            result->flag(text());
            result->flag2(text());
            break;
        case 68: {
            const auto step1 = text();
            const auto step2 = text();
            if (d.numberSteps.contains(step1) || d.numberSteps.contains(step2)) {
                result = make(types::CType::TalkMessage);
                result->comment(d.prop->requiredMsgs().contentName(
                                    types::CType::BranchStepCmp) +
                                ": " + step1 + ", " + step2);
                branchStepCompare[result->eventId()] = {step1, step2};
            } else {
                result = make(types::CType::BranchStepCmp);
                result->step(step1);
                result->step2(step2);
            }
            break;
        }
        case 69:
            result = make(types::CType::BranchFlagCmp);
            result->flag(text());
            result->flag2(text());
            break;
        case 70:
            result = make(types::CType::BranchRandomSelect);
            result->castRange(toCastRanges(f.readUByte()));
            {
                const auto style = f.readUByte();
                if (style & 0x01U) {
                    result->levelMin(static_cast<int>(count()));
                    result->levelMax(static_cast<int>(count()));
                } else {
                    result->levelMin(0);
                    result->levelMax(0);
                }
                result->status((style & 0x02U) ? toStatus(f.readUByte())
                                               : types::Status::None);
            }
            break;
        case 71: {
            result = make(types::CType::BranchKeyCode);
            result->keyCodeRange(toKeyCodeRange(d, f.readUByte(), *result, version));
            const auto cardType = toEffectCardType(f.readUByte());
            result->targetIsSkill(cardType == types::EffectCardType::All ||
                                  cardType == types::EffectCardType::Skill);
            result->targetIsItem(cardType == types::EffectCardType::All ||
                                 cardType == types::EffectCardType::Item);
            result->targetIsBeast(cardType == types::EffectCardType::All ||
                                  cardType == types::EffectCardType::Beast);
            result->targetIsHand(cardType == types::EffectCardType::All ||
                                 cardType == types::EffectCardType::Item ||
                                 cardType == types::EffectCardType::Hand);
            result->keyCode(text());
            break;
        }
        case 72: {
            const auto step = text();
            // Wsn.7はこの項目をwriteExIntで書く
            // 。readExUIntではジグザグ表現自体を値と
            // して解釈してしまう。例えばDの+3が6になる。
            const auto value = version < 7
                ? static_cast<std::int32_t>(f.readUIntL())
                : f.readExInt();
            const auto comparison = toComparison4(f.readUByte());
            if (d.numberSteps.contains(step)) {
                result = make(types::CType::CheckVariant);
                result->expression("@\"" + replaceAll(step, "\"", "\"\"") + "\" " +
                                   (comparison == types::Comparison4::Eq ? "=" :
                                    comparison == types::Comparison4::Ne ? "<>" :
                                    comparison == types::Comparison4::Lt ? ">" : "<") +
                                   " " + std::to_string(static_cast<std::uint32_t>(value)));
            } else {
                result = make(types::CType::CheckStep);
                result->step(step);
                result->stepValue(value);
                result->comparison4(comparison);
            }
            break;
        }
        case 73:
            result = make(types::CType::BranchRound);
            result->comparison3(toComparison3(f.readUByte()));
            result->round(count());
            break;
        case 74:
            result = make(types::CType::MoveBgImage);
            result->cellName(readExString(f));
            {
                const auto control = f.readUByte();
                if (control & 0x01U) {
                    result->positionType(toCoordinateType(f.readUByte()));
                    result->x(extendedSignedValue());
                    result->y(extendedSignedValue());
                }
                if (control & 0x02U) {
                    result->sizeType(toCoordinateType(f.readUByte()));
                    result->width(extendedSignedValue());
                    result->height(extendedSignedValue());
                }
            }
            result->doAnime(true);
            result->ignoreEffectBooster(true);
            result->transition(types::Transition::Default);
            result->transitionSpeed(5);
            break;
        case 75:
            result = make(types::CType::LoseBgImage);
            result->cellName(readExString(f));
            result->doAnime(true);
            result->ignoreEffectBooster(true);
            result->transition(types::Transition::Default);
            result->transitionSpeed(5);
            break;
        case 76: {
            result = make(types::CType::ReplaceBgImage);
            result->cellName(readExString(f));
            const auto cells = readBgImages(d, f, version, false, true);
            result->backgroundImages(cells);
            result->ignoreEffectBooster(true);
            result->transition(types::Transition::Default);
            result->transitionSpeed(5);
            break;
        }
        case 77:
            result = make(types::CType::MoveCard);
            result->cardGroup(readExString(f));
            {
                const auto anime = readBool(f);
                const auto move = readBool(f);
                if (move) {
                    result->positionType(toCoordinateType(f.readUByte()));
                    result->x(extendedSignedValue());
                    result->y(extendedSignedValue());
                    result->cardSpeed(anime ? -1 : 0);
                    result->overrideCardSpeed(!anime);
                }
            }
            break;
        case 78: {
            const auto flags = f.readUByte();
            result = make(types::CType::ChangeEnvironment);
            String warning;
            const auto appendWarning = [&](StringView detail) {
                if (!warning.empty()) warning += "\n";
                warning += utils::tryFormat(
                    static_cast<String>(d.prop->requiredMsgs().convertFromNextWarning),
                    detail);
            };
            if (flags & 0x02U) {
                appendWarning(
                    d.prop->requiredMsgs().convertFromNextWarningForbidSave.value);
            }
            if (flags & 0x04U) {
                appendWarning(
                    d.prop->requiredMsgs().convertFromNextWarningForbidCamp.value);
            }
            if (!warning.empty()) result->comment(std::move(warning));
            result->backpackEnabled((flags & 0x01U) || (flags & 0x04U)
                                        ? types::EnvironmentStatus::Disable
                                        : types::EnvironmentStatus::Enable);
            break;
        }
        default:
            throw std::runtime_error("Unknown classic content type: " + std::to_string(type));
        }
        } catch (const std::exception& error) {
            throw std::runtime_error(
                "classic content type=" + std::to_string(type) +
                " version=" + std::to_string(version) +
                " body=0x" + [&] {
                    std::ostringstream stream;
                    stream << std::hex << bodyOffset;
                    return stream.str();
                }() + ": " + error.what());
        }
        if (const auto comment = info.find("comment"); comment != info.end()) {
            result->comment(comment->second);
        }
        const auto detail = result->detail();
        if (detail.owner) {
            const auto multi = branchMultiStep.find(result->eventId());
            String defaultExpression;
            if (multi != branchMultiStep.end()) {
                std::unordered_set<String> branchNames;
                lib::Array<String> expressions;
                for (const auto& child : children) {
                    if (!child || child->name().empty() ||
                        !std::all_of(child->name().begin(), child->name().end(),
                                     [](const unsigned char value) { return std::isdigit(value) != 0; }) ||
                        !branchNames.insert(child->name()).second) {
                        continue;
                    }
                    expressions.push_back("@\"" + replaceAll(multi->second, "\"", "\"\"") +
                                          "\" = " + child->name());
                }
                if (expressions.empty()) {
                    defaultExpression = "TRUE";
                } else {
                    defaultExpression = "not (";
                    for (std::size_t i = 0; i < expressions.size(); ++i) {
                        if (i != 0U) defaultExpression += " or ";
                        defaultExpression += expressions[i];
                    }
                    defaultExpression += ')';
                }
            }

            for (auto& child : children) {
                if (!child) continue;
                if (detail.nextType == types::CNextType::Text) {
                    auto convertedName = child->name();
                    for (const auto& [stepPath, unused] : d.numberSteps) {
                        (void)unused;
                        convertedName = msgutils::replStepToVariantInText(
                            convertedName, stepPath, stepPath);
                    }
                    child->setName(std::move(convertedName));
                } else if (d.dataVersion >= 7 && detail.nextType == types::CNextType::None &&
                           !child->name().empty()) {
                    child->setName({});
                }

                auto mergeCheck = [&](const OwnerContent& check) {
                    if (child->type() == types::CType::CheckFlag) {
                        check->expression("(" + check->expression() + ") and (FLAGVALUE(\"" +
                                          replaceAll(child->flag(), "\"", "\"\"") + "\"))");
                    } else if (child->type() == types::CType::CheckStep) {
                        String stepExpression = d.numberSteps.contains(child->step())
                                                    ? "@\"" + replaceAll(child->step(), "\"", "\"\"") + "\""
                                                    : "STEPVALUE(\"" + replaceAll(child->step(), "\"", "\"\"") + "\")";
                        const auto comparison = child->comparison4();
                        const String operatorText = comparison == types::Comparison4::Eq ? "=" :
                                                    comparison == types::Comparison4::Ne ? "<>" :
                                                    comparison == types::Comparison4::Lt ? ">" : "<";
                        check->expression("(" + check->expression() + ") and (" + stepExpression +
                                          ' ' + operatorText + ' ' +
                                          std::to_string(child->stepValue()) + ')');
                    } else if (child->type() == types::CType::CheckVariant) {
                        check->expression("(" + check->expression() + ") and (" +
                                          child->expression() + ')');
                    } else {
                        child->setName({});
                        check->add(child);
                        result->add(check);
                        return;
                    }
                    const auto nested = child->next();
                    for (const auto& grandchild : nested) {
                        if (!grandchild) continue;
                        grandchild->setName({});
                        check->add(grandchild);
                    }
                    result->add(check);
                };

                if (multi != branchMultiStep.end()) {
                    auto check = std::make_shared<Content>(types::CType::CheckVariant, "");
                    const auto escaped = replaceAll(multi->second, "\"", "\"\"");
                    if (child->name() == "Default") {
                        check->expression(defaultExpression);
                    } else if (!child->name().empty() &&
                               std::all_of(child->name().begin(), child->name().end(),
                                           [](const unsigned char value) { return std::isdigit(value) != 0; })) {
                        check->expression("@\"" + escaped + "\" = " + child->name());
                    } else {
                        check->expression("FALSE");
                    }
                    mergeCheck(check);
                } else if (const auto compare = branchStepCompare.find(result->eventId());
                           compare != branchStepCompare.end()) {
                    auto check = std::make_shared<Content>(types::CType::CheckVariant, "");
                    const auto stepExpression = [&](const String& step) {
                        const auto escaped = replaceAll(step, "\"", "\"\"");
                        return d.numberSteps.contains(step) ? "@\"" + escaped + "\""
                                                            : "STEPVALUE(\"" + escaped + "\")";
                    };
                    if (child->name() == "<" || child->name() == ">" || child->name() == "=") {
                        check->expression(stepExpression(compare->second.first) + ' ' + child->name() +
                                          ' ' + stepExpression(compare->second.second));
                    } else {
                        check->expression("FALSE");
                    }
                    mergeCheck(check);
                } else {
                    result->add(child);
                }
            }
        }
        children.clear();
        children.push_back(result);
        }
        (void)index;
        return result;
    };
    return readNode();
}

OwnerEventTree readCEventTree(RData& d, ByteIO& f, std::size_t index) {
    auto count = f.readUIntL();
    const ReadVersionScope versionScope(d);
    if (count >= 0x07000000U) {
        count -= 0x07000000U;
        d.dataVersion = 7;
    }
    OwnerEventTree tree;
    for (std::uint32_t i = 0; i < count; ++i) {
        auto start = readContent(d, f, i);
        if (!tree) tree = std::make_shared<EventTree>(start);
        else tree->add(start);
    }
    if (!tree) tree = std::make_shared<EventTree>(String{});
    (void)index;
    return tree;
}

OwnerEventTree readEventTree(RData& d, ByteIO& f, bool, std::size_t index) {
    const auto treeOffset = f.pointer();
    auto count = f.readUIntL();
    const ReadVersionScope versionScope(d);
    if (count >= 0x07000000U) {
        count -= 0x07000000U;
        d.dataVersion = 7;
    }
    OwnerEventTree tree;
    for (std::uint32_t i = 0; i < count; ++i) {
        OwnerContent start;
        try {
            start = readContent(d, f, i);
        } catch (const std::exception& error) {
            std::ostringstream message;
            message << "classic event tree index=" << index << " root=" << i
                    << " tree=0x" << std::hex << treeOffset << ": " << error.what();
            throw std::runtime_error(message.str());
        }
        if (!tree) tree = std::make_shared<EventTree>(start);
        else tree->add(start);
    }
    if (!tree) tree = std::make_shared<EventTree>(String{});
    const auto ignitionCount = readClassicCount(f, d.dataVersion);
    for (std::uint32_t i = 0; i < ignitionCount; ++i) {
        // CardWirthNextはラウンドイベント用のジグザグ
        // 表現も含め、発火コードをExUIntで保存する。元のDの
        // 取り込み処理もここをExUIntで読み、符号付きintへ
        // 代入する。ExIntで復号すると通常の発火値が変わり（
        // 例えば2が1になる）、有効な戦闘イベントを拒否し得る。
        const auto ignition = d.dataVersion < 7
            ? f.readIntL()
            : static_cast<std::int32_t>(f.readExUInt());
        if (ignition < 0) {
            // Dの符号反転はint.minも含めて折り返す。その保存値には符
            // 号なしの減算を使い、C++の符号付きオーバーフローを避ける。
            tree->addRound(std::uint32_t{0} - static_cast<std::uint32_t>(ignition));
        } else {
            switch (ignition) {
            case 1: tree->enter(true); break;
            case 2: tree->escape(true); break;
            case 3: tree->lose(true); break;
            case 4: tree->everyRound(true); break;
            case 5: tree->round0(true); break;
            default: throw std::runtime_error("Unknown classic ignition");
            }
        }
    }
    tree->sortRounds();
    if (d.dataVersion >= 7) {
        tree->keyCodeMatchingType(readBool(f) ? types::MatchingType::And
                                               : types::MatchingType::Or);
    }
    auto keys = d.dataVersion < 7 ? readStrings(f) : readExStrings(f);
    if (d.dataVersion < 7 && !keys.empty() && keys.front() == "MatchingType=All") {
        tree->keyCodeMatchingType(types::MatchingType::And);
        keys.erase(keys.begin());
    }
    lib::Array<system::FKeyCode> keyCodes;
    keyCodes.reserve(keys.size());
    for (auto& key : keys) {
        auto mutableKey = key;
        const system::System defaults;
        const auto kind = (d.prop ? d.prop->sys() : defaults).fireKeyCodeKindRef(mutableKey);
        keyCodes.push_back({std::move(mutableKey), kind});
    }
    tree->keyCodes(std::move(keyCodes));
    (void)index;
    return tree;
}

OwnerArea loadArea(RData& d, ByteIO& f, std::uint64_t fileId) {
    const ReadVersionScope versionScope(d);
    std::uint64_t id = 0;
    String name;
    readAreaHeader(d, f, id, name);
    auto area = std::make_shared<Area>(id, name);
    const auto version = d.dataVersion;
    const auto eventCount = readClassicCount(f, version);
    for (std::uint32_t i = 0; i < eventCount; ++i) {
        area->add(readEventTree(d, f, false, i));
    }
    area->spAuto(!readBool(f));
    const auto cardCount = readClassicCount(f, version);
    for (std::uint32_t i = 0; i < cardCount; ++i) {
        (void)f.readUByte();
        if (version >= 7) {
            (void)f.readUByte();
            (void)f.readUByte();
        }
        const auto embeddedImage = readImage(d, f);
        const auto cardName = readClassicText(f, version);
        if (version >= 7) (void)f.readUByte();
        else (void)f.readUIntL();
        const auto description = readClassicText(f, version);
        const auto eventCountForCard = readClassicCount(f, version);
        area::EventTreeArray cardEvents;
        for (std::uint32_t j = 0; j < eventCountForCard; ++j) {
            cardEvents.push_back(readEventTree(d, f, false, j));
        }
        const auto flag = readClassicText(f, version);
        const auto scale = readClassicCount(f, version);
        const auto x = version >= 7 ? readClassicSigned(f, version) : f.readIntL();
        const auto y = version >= 7 ? readClassicSigned(f, version) : f.readIntL();
        lib::Array<lib::Owner<CardImage>> images;
        if (version <= 2) {
            if (!embeddedImage.empty()) images.push_back(std::make_shared<CardImage>(embeddedImage));
        } else if (version < 7) {
            const auto file = decodePathLegacy(readString(f));
            if (!file.empty()) {
                bool numeric = std::all_of(file.begin(), file.end(), [](char c) {
                    return c >= '0' && c <= '9';
                });
                if (numeric) {
                    try {
                        const auto pc = std::stoull(file);
                        if (pc && pc <= std::numeric_limits<std::uint32_t>::max())
                            images.push_back(std::make_shared<CardImage>(static_cast<std::uint32_t>(pc)));
                    } catch (...) {
                    }
                }
                if (images.empty()) images.push_back(std::make_shared<CardImage>(file));
            }
            if (images.empty() && !embeddedImage.empty()) {
                images.push_back(std::make_shared<CardImage>(embeddedImage));
            }
        } else {
            const auto bits = f.readUByte();
            const auto imageType = bits & 0x0FU;
            if (imageType == 0) {
                if (!embeddedImage.empty()) images.push_back(std::make_shared<CardImage>(embeddedImage));
            } else if (imageType == 1) {
                images.push_back(std::make_shared<CardImage>(decodePathLegacy(readExString(f))));
            } else if (imageType == 2) {
                images.push_back(std::make_shared<CardImage>(f.readUByte()));
            } else {
                throw std::runtime_error("Invalid classic menu-card image type");
            }
            const auto group = readExString(f);
            const auto noAnimation = (bits & 0x10U) != 0;
            const auto expand = (bits & 0x20U) != 0;
            auto card = std::make_shared<area::MenuCard>(
                cardName, expand, images, description, flag, x, y, scale,
                types::LAYER_MENU_CARD, group, noAnimation ? 0 : -1);
            card->addAll(cardEvents);
            area->append(std::move(card));
            continue;
        }
        auto card = std::make_shared<area::MenuCard>(
            cardName, false, images, description, flag, x, y, scale,
            types::LAYER_MENU_CARD, String{}, -1);
        card->addAll(cardEvents);
        area->append(std::move(card));
    }
    for (auto& image : readBgImages(d, f, version, true)) area->append(std::move(image));
    (void)fileId;
    return area;
}

OwnerBattle loadBattle(RData& d, ByteIO& f, std::uint64_t fileId) {
    const ReadVersionScope versionScope(d);
    std::uint64_t id = 0;
    String name;
    readAreaHeader(d, f, id, name);
    const auto version = d.dataVersion;
    auto battle = std::make_shared<Battle>(id, name, String{});
    const auto eventCount = readClassicCount(f, version);
    for (std::uint32_t i = 0; i < eventCount; ++i) {
        try {
            battle->add(readEventTree(d, f, false, i));
        } catch (const std::exception& error) {
            throw std::runtime_error("battle event: " + String(error.what()));
        }
    }
    battle->spAuto(!readBool(f));
    const auto cardCount = readClassicCount(f, version);
    for (std::uint32_t i = 0; i < cardCount; ++i) {
        const auto cardId = readClassicId(f, version);
        // CardWirthはこの件数だけ、v7以降も固定長のリトルエンディ
        // アンUInt32で保存する。周囲の戦闘・カード件数はExUInt
        // を使う。ここをreadClassicCountのままにすると、最
        // 初の敵カードがイベントツリーを持つ時点でストリーム位置がずれる。
        const auto eventCountForCard = f.readUIntL();
        area::EventTreeArray cardEvents;
        for (std::uint32_t j = 0; j < eventCountForCard; ++j) {
            try {
                cardEvents.push_back(readEventTree(d, f, true, j));
            } catch (const std::exception& error) {
                throw std::runtime_error("enemy card " + std::to_string(i) +
                                         " event: " + error.what());
            }
        }
        // 周囲のNextの戦闘構造と異なり、これらの従
        // 来の敵カード配置項目はWsn.7でも固定長。
        const auto flag = readString(f);
        const auto scale = f.readUIntL();
        const auto x = f.readIntL();
        const auto y = f.readIntL();
        const auto bits = version < 7 ? static_cast<Byte>(readBool(f) ? 1 : 0) : f.readUByte();
        const auto escape = (bits & 0x01U) != 0;
        const auto pcImage = (bits & 0x02U) != 0;
        const auto overrideName = (bits & 0x04U) != 0;
        lib::Array<lib::Owner<CardImage>> images;
        if (pcImage) images.push_back(std::make_shared<CardImage>(f.readUByte()));
        String overrideText;
        if (overrideName) overrideText = readExString(f);
        area::ActionMap actions;
        actions[types::ActionCardType::RunAway] = escape;
        auto card = std::make_shared<area::EnemyCard>(
            cardId, actions, flag, x, y, scale, types::LAYER_MENU_CARD, String{}, -1,
            overrideName, overrideText, pcImage, images);
        card->addAll(cardEvents);
        battle->append(std::move(card));
    }
    battle->music(decodePathLegacy(version == 0 ? String("DefBattle.mid")
                                                : readClassicText(f, version)));
    (void)fileId;
    return battle;
}

OwnerPackage loadPackage(RData& d, ByteIO& f, std::uint64_t fileId) {
    d.dataVersion = static_cast<std::int32_t>(f.readUIntL());
    const auto version = d.dataVersion;
    const auto name = readClassicText(f, version);
    const auto id = readClassicId(f, version);
    const auto eventCount = readClassicCount(f, version);
    auto package = std::make_shared<Package>(id, name);
    for (std::uint32_t i = 0; i < eventCount; ++i) package->add(readCEventTree(d, f, i));
    (void)fileId;
    return package;
}

template <typename CardT>
lib::Owner<CardT> readEffCard(RData& d, ByteIO& f) {
    const auto marker = f.readUByte();
    const bool extendedRecord = marker == 0x80U;
    String image;
    String name;
    String description;
    std::uint64_t id = 0;
    if (extendedRecord) {
        d.dataVersion = f.readUByte();
        (void)f.readByte();
        image = readImage(d, f);
        name = readExString(f);
        id = f.readExUInt();
        description = readExString(f);
    } else {
        image = readImage(d, f);
        name = readString(f);
        const auto encoded = f.readUIntL();
        if (encoded < 19999U) {
            d.dataVersion = 0;
            id = encoded;
        } else if (encoded < 39999U) {
            d.dataVersion = 2;
            id = encoded - 20000U;
        } else {
            d.dataVersion = 4;
            id = encoded - 40000U;
        }
        description = readString(f);
    }
    lib::Array<lib::Owner<CardImage>> images;
    if (!image.empty()) images.push_back(std::make_shared<CardImage>(image));
    auto card = std::make_shared<CardT>(&d.prop->sys(), id, name, images, description);
    card->physical(toPhysical(static_cast<std::uint32_t>(
        extendedRecord ? f.readExInt() : f.readUIntL())));
    card->mental(toMental(extendedRecord ? f.readExInt() : f.readIntL()));
    card->spell(readBool(f));
    card->allRange(readBool(f));
    card->target(toCardTarget(f.readUByte()));
    card->effectType(toEffectType(f.readUByte()));
    card->resist(toResist(f.readUByte()));
    card->successRate(extendedRecord ? f.readExInt() : f.readIntL());
    card->visual(toCardVisual(f.readUByte()));
    const auto motionCount = extendedRecord ? static_cast<std::uint32_t>(f.readExUInt())
                                                 : f.readUIntL();
    lib::Array<lib::Owner<Motion>> motions;
    motions.reserve(motionCount);
    for (std::uint32_t i = 0; i < motionCount; ++i) {
        motions.push_back(readMotion(d, d.dataVersion, f, i));
    }
    card->motions(motions);
    card->enhance(types::Enhance::Avoid,
                  extendedRecord ? f.readExInt() : f.readIntL());
    card->enhance(types::Enhance::Resist,
                  extendedRecord ? f.readExInt() : f.readIntL());
    card->enhance(types::Enhance::Defense,
                  extendedRecord ? f.readExInt() : f.readIntL());
    auto sound1 = extendedRecord ? readSound(d, f) : readString(f);
    auto sound2 = extendedRecord ? readSound(d, f) : readString(f);
    if (sound1 == "（なし）" || sound1 == "（なし）.wav") sound1.clear();
    if (sound2 == "（なし）" || sound2 == "（なし）.wav") sound2.clear();
    card->soundPath1(decodePathLegacy(sound1));
    card->soundPath2(decodePathLegacy(sound2));
    const auto keyCount = extendedRecord ? static_cast<std::uint32_t>(f.readExUInt()) : 5U;
    lib::Array<String> keys;
    for (std::uint32_t i = 0; i < keyCount; ++i) {
        const auto key = extendedRecord ? readExString(f) : readString(f);
        keys.push_back(key);
    }
    while (!keys.empty() && keys.back().empty()) keys.pop_back();
    card->keyCodes(keys);
    if (extendedRecord || d.dataVersion > 0) card->premium(toPremium(f.readUByte()));
    if (extendedRecord) (void)f.readByte();
    if (extendedRecord) {
        card->scenario(readExString(f));
        card->author(readExString(f));
        const auto treeCount = static_cast<std::uint32_t>(f.readExUInt());
        for (std::uint32_t i = 0; i < treeCount; ++i) card->add(readCEventTree(d, f, i));
    } else if (d.dataVersion > 2) {
        card->scenario(readString(f));
        card->author(readString(f));
        const auto treeCount = f.readUIntL();
        for (std::uint32_t i = 0; i < treeCount; ++i) card->add(readCEventTree(d, f, i));
    }
    return card;
}

OwnerCast loadCast(RData& d, ByteIO& f, std::uint64_t fileId) {
    const ReadVersionScope versionScope(d);
    const auto marker = f.readUByte();
    const bool extendedRecord = marker == 0x80U;
    String image;
    String name;
    std::uint64_t id = 0;
    if (extendedRecord) {
        d.dataVersion = f.readUByte();
        (void)f.readByte();
        image = readImage(d, f);
        name = readExString(f);
        id = f.readExUInt();
    } else {
        image = readImage(d, f);
        name = readString(f);
        const auto encoded = f.readUIntL();
        if (encoded < 19999U) { d.dataVersion = 0; id = encoded; }
        else if (encoded < 39999U) { d.dataVersion = 2; id = encoded - 20000U; }
        else { d.dataVersion = 4; id = encoded - 40000U; }
    }
    lib::Array<lib::Owner<CardImage>> images;
    if (!image.empty()) images.push_back(std::make_shared<CardImage>(image));
    auto cast = std::make_shared<CastCard>(id, name, images, String{}, 1, 1);
    // バージョンバイトが7未満でも、0x80の印があれば拡張Castデータを選ぶ。
    const auto ex = extendedRecord;
    const auto readU = [&]() { return ex ? static_cast<std::uint32_t>(f.readExUInt()) : f.readUIntL(); };
    const auto readI = [&]() { return ex ? f.readExInt() : f.readIntL(); };
    if (ex) {
        const auto race1 = f.readUByte();
        const auto race2 = f.readUByte();
        cast->weaponResist((race1 & 1U) != 0); cast->magicResist((race1 & 2U) != 0);
        cast->undead((race1 & 4U) != 0); cast->automaton((race1 & 8U) != 0);
        cast->unholy((race1 & 16U) != 0); cast->constructure((race1 & 32U) != 0);
        cast->resist(types::Element::Fire, (race1 & 64U) != 0);
        cast->resist(types::Element::Ice, (race1 & 128U) != 0);
        cast->weakness(types::Element::Fire, (race2 & 1U) != 0);
        cast->weakness(types::Element::Ice, (race2 & 2U) != 0);
    } else {
        cast->weaponResist(readBool(f)); cast->magicResist(readBool(f));
        cast->undead(readBool(f)); cast->automaton(readBool(f));
        cast->unholy(readBool(f)); cast->constructure(readBool(f));
        cast->resist(types::Element::Fire, readBool(f)); cast->resist(types::Element::Ice, readBool(f));
        cast->weakness(types::Element::Fire, readBool(f)); cast->weakness(types::Element::Ice, readBool(f));
    }
    cast->level(readU());
    if (!ex) (void)f.readUIntL();
    cast->desc(ex ? readExString(f, true, true) : readString(f, true, true));
    cast->life(readU()); cast->lifeMax(readU()); cast->paralyze(readU()); cast->poison(readU());
    cast->defaultEnhance(types::Enhance::Avoid, readI());
    cast->defaultEnhance(types::Enhance::Resist, readI());
    cast->defaultEnhance(types::Enhance::Defense, readI());
    for (const auto p : {types::Physical::Dex, types::Physical::Agl, types::Physical::Int,
                         types::Physical::Str, types::Physical::Vit, types::Physical::Min}) {
        cast->physical(p, readU());
    }
    for (const auto m : {types::Mental::Aggressive, types::Mental::Cheerful,
                         types::Mental::Brave, types::Mental::Cautious, types::Mental::Trickish}) {
        cast->mental(m, readI());
    }
    cast->mentality(toMentality(f.readUByte()));
    cast->mentalityRound(readU()); cast->bindRound(readU()); cast->silenceRound(readU());
    cast->faceUpRound(readU()); cast->antiMagicRound(readU());
    for (const auto e : {types::Enhance::Action, types::Enhance::Avoid,
                         types::Enhance::Resist, types::Enhance::Defense}) {
        cast->enhance(e, readI()); cast->enhanceRound(e, readU());
    }
    const auto itemCount = ex ? static_cast<std::uint32_t>(f.readExUInt()) : f.readUIntL();
    for (std::uint32_t i = 0; i < itemCount; ++i) cast->add(loadItem(d, f, i + 1), true);
    const auto skillCount = ex ? static_cast<std::uint32_t>(f.readExUInt()) : f.readUIntL();
    for (std::uint32_t i = 0; i < skillCount; ++i) cast->add(loadSkill(d, f, i + 1), true);
    const auto beastCount = ex ? static_cast<std::uint32_t>(f.readExUInt()) : f.readUIntL();
    for (std::uint32_t i = 0; i < beastCount; ++i) cast->add(loadBeast(d, f, i + 1), true);
    if (ex) (void)f.readByte();
    if (ex || d.dataVersion > 0) {
        const auto couponCount = ex ? static_cast<std::uint32_t>(f.readExUInt()) : f.readUIntL();
        lib::Array<lib::Owner<coupon::Coupon>> coupons;
        for (std::uint32_t i = 0; i < couponCount; ++i) {
            const auto couponName = readString(f);
            const auto couponValue = f.readIntL();
            coupons.push_back(
                std::make_shared<coupon::Coupon>(couponName, couponValue));
        }
        cast->coupons(coupons);
    }
    (void)fileId;
    return cast;
}

OwnerSkill loadSkill(RData& d, ByteIO& f, std::uint64_t fileId) {
    const ReadVersionScope versionScope(d);
    auto card = readEffCard<SkillCard>(d, f);
    if (d.dataVersion > 2) card->hold(readBool(f));
    card->level(d.dataVersion >= 7 ? static_cast<std::uint32_t>(f.readExInt()) : f.readUIntL());
    card->useLimit(d.dataVersion >= 7 ? static_cast<std::uint32_t>(f.readExUInt()) : f.readUIntL());
    if (d.dataVersion >= 7) (void)readBool(f);
    card->price(400U + card->level() * 200U);
    (void)fileId;
    return card;
}

OwnerItem loadItem(RData& d, ByteIO& f, std::uint64_t fileId) {
    const ReadVersionScope versionScope(d);
    auto card = readEffCard<ItemCard>(d, f);
    if (d.dataVersion > 2) card->hold(readBool(f));
    if (d.dataVersion >= 7) {
        card->useLimit(f.readExUInt()); card->useLimitMax(f.readExUInt()); card->price(f.readExUInt());
        card->enhanceOwner(types::Enhance::Avoid, f.readExInt());
        card->enhanceOwner(types::Enhance::Resist, f.readExInt());
        card->enhanceOwner(types::Enhance::Defense, f.readExInt());
    } else {
        card->useLimit(f.readUIntL()); card->useLimitMax(f.readUIntL()); card->price(f.readUIntL());
        card->enhanceOwner(types::Enhance::Avoid, f.readUIntL());
        card->enhanceOwner(types::Enhance::Resist, f.readUIntL());
        card->enhanceOwner(types::Enhance::Defense, f.readUIntL());
    }
    (void)fileId;
    return card;
}

OwnerBeast loadBeast(RData& d, ByteIO& f, std::uint64_t fileId) {
    const ReadVersionScope versionScope(d);
    auto card = readEffCard<BeastCard>(d, f);
    if (d.dataVersion > 2) (void)readBool(f);
    card->useLimit(d.dataVersion >= 7 ? f.readExUInt() : f.readUIntL());
    card->price(1000);
    if (d.dataVersion >= 7) (void)f.readByte();
    (void)fileId;
    return card;
}

OwnerInfo loadInfo(RData& d, ByteIO& f, std::uint64_t fileId) {
    const ReadVersionScope versionScope(d);
    const auto marker = f.readUByte();
    String image;
    String name;
    String description;
    std::uint64_t id = 0;
    if (marker == 0x80U) {
        d.dataVersion = f.readUByte();
        (void)f.readByte();
        image = readImage(d, f); name = readExString(f); id = f.readExUInt(); description = readExString(f);
    } else {
        image = readImage(d, f); name = readString(f);
        const auto encoded = f.readUIntL();
        if (encoded < 19999U) { d.dataVersion = 0; id = encoded; }
        else if (encoded < 39999U) { d.dataVersion = 2; id = encoded - 20000U; }
        else { d.dataVersion = 4; id = encoded - 40000U; }
        description = readString(f);
    }
    lib::Array<lib::Owner<CardImage>> images;
    if (!image.empty()) images.push_back(std::make_shared<CardImage>(image));
    (void)fileId;
    return std::make_shared<InfoCard>(id, name, images, description);
}

namespace {

[[nodiscard]] String readUtf8Text(const std::filesystem::path& file) {
    // .wexの読み込みはDのstd.file.readTextのUTF-8契約を使う。ユー
    // ザー作成の旧形式テキスト向けの、Shift-JISを優先する別の補助処理は使わない。
    const auto bytes = utils::readBinary(file);
    auto text = bytes.empty() ? String{} : byteString(binary::ByteView(bytes));
    enumutils::validateUtf8(text);
    return text;
}

[[nodiscard]] OwnerSummary requireWexSummary(OwnerSummary model) {
    if (!model) throw std::invalid_argument("Null classic extension-data summary");
    return model;
}

}  // namespace

OwnerSummary loadLScenario(StringView path, StringView skinType,
                           StringView skinName, const CProps& prop,
                           const LoadOption& opt,
                           lib::Array<String>& errorFiles,
                           std::int32_t& dataVersion, StringView newName) {
    perf::TraceScope trace(__FILE__, __LINE__);
    errorFiles.clear();
    dataVersion = 0;
    const auto scenarioPath = fsPath(path);
    const auto summaryPath = scenarioPath / "Summary.wsm";
    RData data(prop, opt.cardOnly, String(path), String(skinType), String(skinName),
               opt.numberStepToVariantThreshold);
    OwnerSummary summary;
    std::uint64_t startArea = 0;
    if (std::filesystem::is_regular_file(summaryPath)) {
        const auto bytes = utils::readBinary(summaryPath);
        ByteIO f{binary::ByteView(bytes)};
        summary = loadSummary(data, f, startArea);
    } else {
        if (newName.empty()) throw summary::SummaryException("Not Scenario: " + String(path));
        summary = std::make_shared<Summary>(String(newName), String(skinType),
                                             String(skinName), String(path), false, true);
    }
    dataVersion = data.dataVersion;

    if (opt.summaryOnly) return summary;

    struct LoadState {
        RData data;
        std::vector<std::filesystem::path> files;
        std::uintmax_t bytes{};
        lib::Array<OwnerArea> areas;
        lib::Array<OwnerBattle> battles;
        lib::Array<OwnerPackage> packages;
        lib::Array<OwnerCast> casts;
        lib::Array<OwnerSkill> skills;
        lib::Array<OwnerItem> items;
        lib::Array<OwnerBeast> beasts;
        lib::Array<OwnerInfo> infos;
        lib::Array<String> errors;
    } first{data}, second{data};

    if (std::filesystem::is_directory(scenarioPath)) {
        for (const auto& entry : std::filesystem::directory_iterator(scenarioPath)) {
            if (entry.is_regular_file() &&
                utils::cfnmatch(utf8Path(entry.path().extension()), ".wid")) {
                std::error_code sizeError;
                const auto size = entry.file_size(sizeError);
                auto& state = first.bytes < second.bytes ? first : second;
                state.files.push_back(entry.path());
                if (!sizeError) state.bytes += size;
            }
        }
    }

    const auto loadFiles = [&](LoadState& state) {
        for (const auto& file : state.files) {
            // 並列読み込みは通常のjoinを経由して終了する。補助のstd::thread
            // から取消例外を外へ送出してはならない。
            if (opt.isCancelled()) return;
            try {
                const auto base = utf8Path(file.filename());
                std::uint64_t id = 0;
                const auto type = getType(base, id);
                const auto bytes = utils::readBinary(file);
                ByteIO f{binary::ByteView(bytes)};
                if (opt.processFunc) opt.processFunc(summary->scenarioName(), base);
                // DのloadLScenarioに合わせ、カード専用モードでもArea/Battle/Package
                // の各メンバーを読み込んで報告してから、変換を省略する。
                if (opt.cardOnly && (type == ResourceType::Area ||
                                     type == ResourceType::Battle ||
                                     type == ResourceType::Package)) {
                    continue;
                }
                switch (type) {
                case ResourceType::Area: state.areas.push_back(loadArea(state.data, f, id)); break;
                case ResourceType::Battle: state.battles.push_back(loadBattle(state.data, f, id)); break;
                case ResourceType::Package: state.packages.push_back(loadPackage(state.data, f, id)); break;
                case ResourceType::Cast: state.casts.push_back(loadCast(state.data, f, id)); break;
                case ResourceType::Skill: state.skills.push_back(loadSkill(state.data, f, id)); break;
                case ResourceType::Item: state.items.push_back(loadItem(state.data, f, id)); break;
                case ResourceType::Beast: state.beasts.push_back(loadBeast(state.data, f, id)); break;
                case ResourceType::Info: state.infos.push_back(loadInfo(state.data, f, id)); break;
                case ResourceType::Unknown: break;
                }
            } catch (const std::exception& exception) {
                const auto member = utf8Path(file.filename());
                state.errors.push_back(member);
                if (opt.issueFunc) opt.issueFunc(member, exception.what());
            } catch (...) {
                const auto member = utf8Path(file.filename());
                state.errors.push_back(member);
                if (opt.issueFunc) opt.issueFunc(member, "unknown resource conversion error");
            }
        }
    };

    if (opt.doubleIO && !second.files.empty()) {
        std::thread worker([&] { loadFiles(second); });
        loadFiles(first);
        worker.join();
    } else {
        loadFiles(first);
        loadFiles(second);
    }

    const auto appendSorted = [&summary](auto left, auto right) {
        left.insert(left.end(), std::make_move_iterator(right.begin()),
                    std::make_move_iterator(right.end()));
        std::sort(left.begin(), left.end(), [](const auto& lhs, const auto& rhs) {
            if (!lhs) return static_cast<bool>(rhs);
            if (!rhs) return false;
            return lhs->id() < rhs->id();
        });
        for (auto& value : left) if (value) summary->add(std::move(value), false);
    };
    appendSorted(std::move(first.areas), std::move(second.areas));
    appendSorted(std::move(first.battles), std::move(second.battles));
    appendSorted(std::move(first.packages), std::move(second.packages));
    appendSorted(std::move(first.casts), std::move(second.casts));
    appendSorted(std::move(first.skills), std::move(second.skills));
    appendSorted(std::move(first.items), std::move(second.items));
    appendSorted(std::move(first.beasts), std::move(second.beasts));
    appendSorted(std::move(first.infos), std::move(second.infos));

    loadComment(summary);
    loadImageRef(summary);
    loadCardRef(summary);
    loadTemplate(summary);
    summary->startArea(startArea);
    summary->resetChanged();
    errorFiles.insert(errorFiles.end(),
                      std::make_move_iterator(first.errors.begin()),
                      std::make_move_iterator(first.errors.end()));
    errorFiles.insert(errorFiles.end(),
                      std::make_move_iterator(second.errors.begin()),
                      std::make_move_iterator(second.errors.end()));
    dataVersion = data.dataVersion;
    return summary;
}

void loadComment(const OwnerSummary& summary) {
    const auto model = requireWexSummary(summary);
    const auto file = fsPath(utils::buildPath(model->scenarioPath(), "Comment.wex"));
    if (!classicPathExists(file)) return;
    auto root = xml::XNode::parse(readUtf8Text(file));
    if (root.name() != "comments") return;
    root.setLiveHandler("comment", [model](xml::XNode& node) {
        const auto path = node.attr<String>("path", false, String(path::INVALID_CWX_PATH));
        if (path == path::INVALID_CWX_PATH) return;
        const auto forEvents = node.attr<bool>("event", false, false);
        const auto target = model->findCWXPath(path);
        if (forEvents) {
            if (auto owner = std::dynamic_pointer_cast<event::EventTreeOwner>(target)) {
                const auto value = node.value();
                if (value.empty()) return;
                if (!enumutils::stripWhiteSpace(owner->commentForEvents()).empty())
                    owner->commentForEvents(owner->commentForEvents() + "\n");
                owner->commentForEvents(owner->commentForEvents() + value);
            }
        } else if (auto comment = dynamic_cast<path::Commentable*>(target.get())) {
            const auto value = replaceAll(replaceAll(node.value(), "\r\n", "\n"), "\r", "");
            if (value.empty()) return;
            if (!enumutils::stripWhiteSpace(comment->comment()).empty())
                comment->comment(comment->comment() + "\n");
            comment->comment(comment->comment() + value);
        }
    });
    root.parseLive();
}

void loadImageRef(const OwnerSummary& summary) {
    const auto model = requireWexSummary(summary);
    const auto file = fsPath(utils::buildPath(model->scenarioPath(), "ImageRef.wex"));
    if (!classicPathExists(file)) return;
    auto root = xml::XNode::parse(readUtf8Text(file));
    if (root.name() != "imageRefs") return;
    root.setLiveHandler("imageRef", [model](xml::XNode& node) {
        const auto path = node.attr<String>("path", false, String(path::INVALID_CWX_PATH));
        if (path == path::INVALID_CWX_PATH) return;
        const auto target = model->findCWXPath(path);
        const auto images = [&node] {
            lib::Array<lib::Owner<CardImage>> result;
            if (!node.value().empty()) result.push_back(std::make_shared<CardImage>(utils::decodePath(node.value())));
            return result;
        };
        if (auto card = std::dynamic_pointer_cast<card::Card>(target)) {
            card->paths(images());
        }
        if (auto menu = std::dynamic_pointer_cast<area::MenuCard>(target)) {
            menu->paths(images());
        }
        if (auto rootSummary = std::dynamic_pointer_cast<Summary>(target)) {
            rootSummary->imagePaths(images());
        }
    });
    root.parseLive();
}

void loadCardRef(const OwnerSummary& summary) {
    const auto model = requireWexSummary(summary);
    const auto file = fsPath(utils::buildPath(model->scenarioPath(), "CardRef.wex"));
    if (!classicPathExists(file)) return;
    auto root = xml::XNode::parse(readUtf8Text(file));
    if (root.name() != "cardRefs") return;
    root.setLiveHandler("maxNest", [model](xml::XNode& node) {
        const auto path = node.attr<String>("path", false, String(path::INVALID_CWX_PATH));
        if (path == path::INVALID_CWX_PATH) return;
        auto target = model->findCWXPath(path);
        if (auto motion = std::dynamic_pointer_cast<Motion>(target)) {
            try { motion->maxNest(parseUnsigned<std::uint32_t>(node.value())); }
            catch (const ClassicReferenceConversionError& error) {
                (void)utils::printStackTrace();
                utils::debugln(error.what());
            }
        }
    });
    root.setLiveHandler("cardRef", [model](xml::XNode& node) {
        const auto path = node.attr<String>("path", false, String(path::INVALID_CWX_PATH));
        if (path == path::INVALID_CWX_PATH) return;
        const auto target = model->findCWXPath(path);
        try {
            const auto id = parseUnsigned<std::uint64_t>(node.value());
            if (auto skill = std::dynamic_pointer_cast<SkillCard>(target)) {
                const auto hold = skill->hold();
                skill->clearData();
                skill->linkId(id);
                skill->hold(hold);
            }
            if (auto item = std::dynamic_pointer_cast<ItemCard>(target)) {
                const auto hold = item->hold();
                item->clearData();
                item->linkId(id);
                item->hold(hold);
            }
            if (auto beast = std::dynamic_pointer_cast<BeastCard>(target)) {
                beast->clearData();
                beast->linkId(id);
            }
        } catch (const ClassicReferenceConversionError& error) {
            (void)utils::printStackTrace();
            utils::debugln(error.what());
        }
    });
    root.parseLive();
}

void loadTemplate(const OwnerSummary& summary) {
    const auto model = requireWexSummary(summary);
    const auto file = fsPath(utils::buildPath(model->scenarioPath(), "Template.wex"));
    if (!classicPathExists(file)) return;
    auto root = xml::XNode::parse(readUtf8Text(file));
    if (root.name() != "templates") return;
    root.setLiveHandler("eventTemplates", [model](xml::XNode& group) {
        auto templates = std::make_shared<lib::Array<structs::EvTemplate>>();
        group.setLiveHandler("eventTemplate", [templates](xml::XNode& child) {
            structs::EvTemplate value;
            value.fromNode(child);
            templates->push_back(std::move(value));
        });
        group.parseLive();
        model->eventTemplates(
            lib::SharedSlice<structs::EvTemplate>(std::move(*templates)));
    });
    root.parseLive();
}

String readPartyName(const CProps& prop, StringView wpl) {
    const auto bytes = utils::readBinary(fsPath(wpl));
    ByteIO f{binary::ByteView(bytes)};
    RData d(prop, false, {}, {}, {}, 0);
    (void)f.readUShortL();
    (void)readString(f);
    (void)readImage(d, f);
    (void)readString(f);
    return readString(f);
}

bool isDebugYado(const CProps& prop, StringView yadoDir) {
    const auto path = fsPath(yadoDir) / "Environment.wyd";
    const auto bytes = utils::readBinary(path);
    ByteIO f{binary::ByteView(bytes)};
    RData d(prop, false, {}, {}, {}, 0);
    (void)readString(f);
    return f.readUByte() == 2;
}

String saveTemplate(const Summary& summary) {
    if (summary.eventTemplates().empty()) return {};
    auto root = xml::XNode::create("templates");
    root.newAttr("dataVersion", 1);
    auto group = root.newElement("eventTemplates");
    for (const auto& value : summary.eventTemplates()) value.toNode(group);
    return root.text();
}

void putExData(SData& d, const lib::Owner<CWXPath>& path) {
    const auto addComment = [&d](const lib::Owner<CWXPath>& object) {
        if (!object) return;
        if (auto comment = dynamic_cast<path::Commentable*>(object.get()); comment &&
            !comment->comment().empty()) {
            d.comment[CommentKey(object->cwxPath(true), false)] = comment->comment();
        }
        if (auto events = std::dynamic_pointer_cast<event::EventTreeOwner>(object);
            events && !events->commentForEvents().empty()) {
            d.comment[CommentKey(object->cwxPath(true), true)] = events->commentForEvents();
        }
        if (auto motion = std::dynamic_pointer_cast<Motion>(object);
            motion && motion->maxNest() != 1U) {
            d.maxNest[object->cwxPath(true)] = motion->maxNest();
        }
        if (auto card = std::dynamic_pointer_cast<card::EffectCard>(object);
            card && card->linkId()) d.cardRef[object->cwxPath(true)] = card->linkId();
        if (auto card = std::dynamic_pointer_cast<card::Card>(object);
            card && d.saveInnerImagePath) {
            const auto images = card->paths();
            if (!images.empty() && images.front() &&
                images.front()->type == types::CardImageType::File &&
                !images.front()->path().empty() &&
                !utils::isBinImg(images.front()->path())) {
                d.imageRef[object->cwxPath(true)] = encodePathLegacy(images.front()->path());
            }
        }
        if (auto summ = std::dynamic_pointer_cast<Summary>(object);
            summ && d.saveInnerImagePath) {
            const auto images = summ->imagePaths();
            if (!images.empty() && images.front() &&
                images.front()->type == types::CardImageType::File &&
                !images.front()->path().empty() &&
                !utils::isBinImg(images.front()->path())) {
                d.imageRef[object->cwxPath(true)] = encodePathLegacy(
                    images.front()->path());
            }
        }
    };

    auto current = path;
    while (current) {
        addComment(current);
        const auto children = current->cwxChilds();
        if (children.size() == 1U) {
            // Dの単一子の末尾ループに合わせ、長いContent連鎖でネイティブの呼び出しスタ
            // ックを消費しないようにする。PlayerCardEventsの枝刈りは維持する。
            current = children.front().lock();
            if (std::dynamic_pointer_cast<area::PlayerCardEvents>(current)) return;
            continue;
        }
        for (const auto& child : children) {
            if (auto owner = child.lock();
                owner && !std::dynamic_pointer_cast<area::PlayerCardEvents>(owner)) {
                putExData(d, owner);
            }
        }
        break;
    }
}

namespace {

[[nodiscard]] std::int32_t saveVersion(const SData& d) {
    return d.opt ? static_cast<std::int32_t>(d.opt->dataVersion) : 4;
}

void writeClassicCount(ByteIO& f, std::uint32_t value, std::int32_t version) {
    if (version < 7) f.writeL(value);
    else f.writeExUInt(static_cast<std::int32_t>(value));
}

void writeClassicSigned(ByteIO& f, std::int32_t value, std::int32_t version) {
    if (version < 7) f.writeL(value);
    else f.writeExInt(value);
}

void writeClassicId(ByteIO& f, std::uint64_t value, std::int32_t version) {
    if (version < 7) f.writeL(static_cast<std::uint32_t>(value));
    else f.writeExUInt(static_cast<std::int32_t>(value));
}

void writeClassicText(ByteIO& f, StringView value, std::int32_t version,
                      bool lns = false) {
    if (version < 7) writeString(f, value, lns);
    else writeExString(f, value, lns);
}

[[nodiscard]] Byte contentTypeByte(types::CType type, std::int32_t version) {
    using types::CType;
    switch (type) {
    case CType::Start: return 0;
    case CType::LinkStart: return 1;
    case CType::StartBattle: return 2;
    case CType::End: return 3;
    case CType::EndBadEnd: return 4;
    case CType::ChangeArea: return 5;
    case CType::TalkMessage: return 6;
    case CType::PlayBgm: return 7;
    case CType::ChangeBgImage: return 8;
    case CType::PlaySound: return 9;
    case CType::Wait: return 10;
    case CType::Effect: return 11;
    case CType::BranchSelect: return 12;
    case CType::BranchAbility: return 13;
    case CType::BranchRandom: return 14;
    case CType::BranchFlag: return 15;
    case CType::SetFlag: return 16;
    case CType::BranchMultiStep: return 17;
    case CType::SetStep: return 18;
    case CType::BranchCast: return 19;
    case CType::BranchItem: return 20;
    case CType::BranchSkill: return 21;
    case CType::BranchInfo: return 22;
    case CType::BranchBeast: return 23;
    case CType::BranchMoney: return 24;
    case CType::BranchCoupon: return 25;
    case CType::GetCast: return 26;
    case CType::GetItem: return 27;
    case CType::GetSkill: return 28;
    case CType::GetInfo: return 29;
    case CType::GetBeast: return 30;
    case CType::GetMoney: return 31;
    case CType::GetCoupon: return 32;
    case CType::LoseCast: return 33;
    case CType::LoseItem: return 34;
    case CType::LoseSkill: return 35;
    case CType::LoseInfo: return 36;
    case CType::LoseBeast: return 37;
    case CType::LoseMoney: return 38;
    case CType::LoseCoupon: return 39;
    case CType::TalkDialog: return 40;
    case CType::SetStepUp: return 41;
    case CType::SetStepDown: return 42;
    case CType::ReverseFlag: return 43;
    case CType::BranchStep: return 44;
    case CType::ElapseTime: return 45;
    case CType::BranchLevel: return 46;
    case CType::BranchStatus: return 47;
    case CType::BranchPartyNumber: return 48;
    case CType::ShowParty: return 49;
    case CType::HideParty: return 50;
    case CType::EffectBreak: return 51;
    case CType::CallStart: return 52;
    case CType::LinkPackage: return 53;
    case CType::CallPackage: return 54;
    case CType::BranchArea: return 55;
    case CType::BranchBattle: return 56;
    case CType::BranchCompleteStamp: return 57;
    case CType::GetCompleteStamp: return 58;
    case CType::LoseCompleteStamp: return 59;
    case CType::BranchGossip: return 60;
    case CType::GetGossip: return 61;
    case CType::LoseGossip: return 62;
    case CType::BranchIsBattle: return 63;
    case CType::Redisplay: return 64;
    case CType::CheckFlag: return 65;
    case CType::SubstituteStep: return 66;
    case CType::SubstituteFlag: return 67;
    case CType::BranchStepCmp: return 68;
    case CType::BranchFlagCmp: return 69;
    case CType::BranchRandomSelect: return 70;
    case CType::BranchKeyCode: return 71;
    case CType::CheckStep: return 72;
    case CType::BranchRound: return 73;
    case CType::MoveBgImage: return version >= 7 ? 74 : 6;
    case CType::LoseBgImage: return version >= 7 ? 75 : 6;
    case CType::ReplaceBgImage: return version >= 7 ? 76 : 6;
    case CType::MoveCard: return version >= 7 ? 77 : 6;
    case CType::ChangeEnvironment: return version >= 7 ? 78 : 6;
    case CType::BranchMultiCoupon:
    case CType::BranchMultiRandom:
    case CType::BranchVariant:
    case CType::SetVariant:
    case CType::CheckVariant:
    case CType::CustomXmlContent:
        return 6;
    }
    throw std::invalid_argument("Unknown content type");
}

}  // namespace

void writeComments(SData& d, const lib::Owner<CWXPath>& path) {
    // コメントはClassicのバイナリレコードではなくComment.wex
    // に保存する。各レコードの境界にこのフックを置き、対応するCWXPath
    // を所有する呼び出し側からもメタデータを渡せるようにする。
    if (path) putExData(d, path);
}

void writeSummary(SData& d, ByteIO& f, const Summary& summary) {
    const auto version = saveVersion(d);
    String image;
    if (!summary.imagePaths().empty() && summary.imagePaths().front()) {
        image = summary.imagePaths().front()->path();
    }
    writeImage(d, f, {}, image);
    writeString(f, summary.scenarioName());
    writeString(f, summary.desc(), true);
    writeString(f, summary.author());
    // Classicの書き込み処理は、WSNファイルでも必要クー
    // ポンを従来の文字列配列形式で保持する。バージョン7で拡張
    // 整数・文字列へ切り替えるのは、後続のサマリ内の集合だけ。
    writeStrings(f, summary.rCoupons());
    f.writeL(summary.rCouponNum());
    f.writeL(static_cast<std::uint32_t>(summary.startArea() + (version < 7 ? 40000U : 70000U)));
    const auto root = summary.flagDirRoot();
    if (version < 7) {
        const auto steps = root ? sortedVariables(root->allSteps(), d.logicalSort) : lib::Array<flag::StepPtr>{};
        f.writeL(static_cast<std::uint32_t>(steps.size()));
        for (const auto& step : steps) {
            writeString(f, step->path());
            f.writeL(std::min<std::uint32_t>(step->select(), 9U));
            for (std::uint32_t i = 0; i < 10; ++i) {
                writeString(f, i < step->count() ? step->getValue(i)
                                                 : "Step - " + std::to_string(i + 1));
            }
        }
        const auto flags = root ? sortedVariables(root->allFlags(), d.logicalSort) : lib::Array<flag::FlagPtr>{};
        f.writeL(static_cast<std::uint32_t>(flags.size()));
        for (const auto& flag : flags) {
            writeString(f, flag->path()); writeBool(f, flag->onOff());
            writeString(f, flag->on()); writeString(f, flag->off());
        }
        f.writeL(0U); f.writeL(summary.levelMin()); f.writeL(summary.levelMax());
    } else {
        for (int i = 0; i < 20; ++i) f.writeL(static_cast<Byte>(0));
        const auto steps = root ? sortedVariables(root->allSteps(), d.logicalSort) : lib::Array<flag::StepPtr>{};
        const auto variants = root ? sortedVariables(root->allVariants(), d.logicalSort) : lib::Array<flag::VariantPtr>{};
        const auto variantCount = std::count_if(
            variants.begin(), variants.end(), [](const auto& value) { return value != nullptr; });
        f.writeExUInt(static_cast<std::int32_t>(steps.size() + variantCount));
        for (const auto& step : steps) {
            writeExString(f, step->path());
            const auto values = step->values();
            const bool numeric = !step->expandSPChars() && [&] {
                for (std::size_t i = 0; i < values.size(); ++i)
                    if (values[i] != std::to_string(i)) return false;
                return true;
            }();
            f.write(static_cast<Byte>(numeric ? 2 : step->expandSPChars() ? 0x81 : 1));
            if (numeric) {
                f.writeExUInt(static_cast<std::uint32_t>(values.size()));
                f.writeExUInt(step->select());
                continue;
            }
            if (step->expandSPChars()) f.write(Byte{2});
            f.writeExUInt(static_cast<std::int32_t>(values.size()));
            for (const auto& value : values) writeExString(f, value);
            f.writeExUInt(static_cast<std::int32_t>(step->select()));
        }
        // 意図的な拡張。C++モデルはコモン変数に対応するが、Classic
        // のv7形式にはフラグとステップしかない。そのため
        // 数値コモンは黙って破棄せず、数値ステップで表す。
        for (const auto& variant : variants) {
            if (!variant) continue;
            writeExString(f, variant->path());
            f.writeL(static_cast<Byte>(2));
            const auto numeric = std::isfinite(variant->numVal())
                                     ? std::max(0.0, variant->numVal())
                                     : 0.0;
            const auto selected = static_cast<std::uint32_t>(std::llround(numeric));
            const auto valueCount = std::max<std::uint32_t>(10U, selected + 1U);
            f.writeExUInt(static_cast<std::int32_t>(valueCount));
            f.writeExUInt(static_cast<std::int32_t>(selected));
        }
        const auto flags = root ? sortedVariables(root->allFlags(), d.logicalSort) : lib::Array<flag::FlagPtr>{};
        f.writeExUInt(static_cast<std::int32_t>(flags.size()));
        for (const auto& flag : flags) {
            writeExString(f, flag->path()); writeBool(f, flag->onOff());
            writeExString(f, flag->on()); writeExString(f, flag->off());
        }
        f.writeExUInt(static_cast<std::int32_t>(summary.levelMin()));
        f.writeExUInt(static_cast<std::int32_t>(summary.levelMax()));
    }
}

void writeMotion(SData& d, ByteIO& f, const Motion& motion) {
    using motion::MType;
    const auto type = motion.type();
    Byte table = 0, subtype = 0;
    constexpr std::array<MType, 3> recovery{MType::Heal, MType::Damage, MType::Absorb};
    constexpr std::array<MType, 4> statuses{MType::Paralyze, MType::DisParalyze,
                                             MType::Poison, MType::DisPoison};
    constexpr std::array<MType, 6> mentality{MType::Sleep, MType::Confuse, MType::Overheat,
                                              MType::Brave, MType::Panic, MType::Normal};
    constexpr std::array<MType, 8> conditions{MType::Bind, MType::DisBind, MType::Silence,
                                               MType::DisSilence, MType::FaceUp,
                                               MType::FaceDown, MType::AntiMagic,
                                               MType::DisAntiMagic};
    constexpr std::array<MType, 4> enhancements{MType::EnhanceAction, MType::EnhanceAvoid,
                                                  MType::EnhanceResist, MType::EnhanceDefense};
    constexpr std::array<MType, 3> vanish{MType::VanishTarget, MType::VanishCard,
                                          MType::VanishBeast};
    constexpr std::array<MType, 9> cards{MType::DealAttackCard, MType::DealPowerfulAttackCard,
                                         MType::DealCriticalAttackCard, MType::DealFeintCard,
                                         MType::DealDefenseCard, MType::DealDistanceCard,
                                         MType::DealConfuseCard, MType::DealSkillCard,
                                         MType::CancelAction};
    const auto find = [](const auto& values, MType value) -> int {
        for (std::size_t i = 0; i < values.size(); ++i) if (values[i] == value) return static_cast<int>(i);
        return -1;
    };
    int type_index = find(recovery, type);
    if (type == MType::NoEffect) { table = 0; subtype = 1; }
    else if (type_index >= 0) { table = 0; subtype = static_cast<Byte>(type_index); }
    else if ((type_index = find(statuses, type)) >= 0) { table = 1; subtype = static_cast<Byte>(type_index); }
    else if (type == MType::GetSkillPower || type == MType::LoseSkillPower) { table = 2; subtype = type == MType::LoseSkillPower; }
    else if ((type_index = find(mentality, type)) >= 0) { table = 3; subtype = static_cast<Byte>(type_index); }
    else if ((type_index = find(conditions, type)) >= 0) { table = 4; subtype = static_cast<Byte>(type_index); }
    else if ((type_index = find(enhancements, type)) >= 0) { table = 5; subtype = static_cast<Byte>(type_index); }
    else if ((type_index = find(vanish, type)) >= 0) { table = 6; subtype = static_cast<Byte>(type_index); }
    else if ((type_index = find(cards, type)) >= 0) { table = 7; subtype = static_cast<Byte>(type_index); }
    else if (type == MType::SummonBeast) { table = 8; subtype = 0; }
    else throw std::invalid_argument("Motion cannot be represented in classic files");
    const auto version = saveVersion(d);
    f.write(table);
    if (version > 2) for (const auto value : {Byte{8}, Byte{4}, Byte{0}, Byte{0}, Byte{0}}) f.write(value);
    f.write(::cwxeditor::cwx::cwl::fromElement(
        static_cast<types::Element>(motion.element())));
    if (table != 8) f.write(subtype);
    if (table == 0 || table == 1) {
        f.write(::cwxeditor::cwx::cwl::fromDamageType(
            static_cast<types::DamageType>(motion.damageType())));
        f.writeL(motion.uValue());
    } else if (table == 3 || table == 4) {
        f.writeL(motion.detail().use(motion::MArg::Round) ? static_cast<std::uint32_t>(motion.round()) : 10U);
    } else if (table == 5) {
        f.writeL(static_cast<std::uint32_t>(motion.aValue())); f.writeL(static_cast<std::uint32_t>(motion.round()));
    } else if (table == 8) {
        const auto beast = motion.beast();
        if (!beast) {
            f.writeL(static_cast<std::uint32_t>(0));
        } else if (beast->linkId() != 0) {
            // 再帰する召喚獣レコードには、D実装と
            // 同じリンクの入れ子の上限を適用する。
            auto& nested = d.nestCount[beast->linkId()];
            ++nested;
            if (nested <= motion.maxNest()) {
                f.writeL(static_cast<std::uint32_t>(1));
                writeBeast(d, f, *beast);
            } else {
                f.writeL(static_cast<std::uint32_t>(0));
            }
            if (nested <= 1) d.nestCount.erase(beast->linkId());
            else --nested;
        } else {
            f.writeL(static_cast<std::uint32_t>(1));
            writeBeast(d, f, *beast);
        }
    }
}

namespace {

void writeContentPayload(SData& d, ByteIO& f, const Content& content,
                         const std::int32_t version) {
    const bool extended = version >= 7;
    const auto type = content.type();
    const auto unsupported_message = [&] {
        return utils::lastRet(d.prop ? d.prop->requiredMsgs().contentName(type)
                                    : event::contentDetail(type).names.front());
    };

    const auto text = [&](StringView value, bool line = false) {
        if (extended) writeExString(f, value, line);
        else writeString(f, value, line);
    };
    const auto count = [&](std::uint32_t value) { writeClassicCount(f, value, version); };
    const auto id = [&](std::uint64_t value) { writeClassicId(f, value, version); };
    const auto signedValue = [&](std::int32_t value) { writeClassicSigned(f, value, version); };
    const auto imagePaths = [&]() {
        const auto backgrounds = content.backgroundImages();
        if (!backgrounds.empty()) return snapshotBackgrounds(backgrounds);
        lib::Array<ConstOwnerBgImage> images;
        for (const auto& path : content.backs()) {
            images.push_back(std::make_shared<background::ImageCell>(
                path, String{}, 0, 0, 632, 420, false));
        }
        return images;
    };
    const auto writeCardId = [&](std::uint64_t value) { id(value); };
    const auto writeRange = [&](types::Range value) {
        f.write(::cwxeditor::cwx::cwl::fromRange(value));
    };
    const auto writeTextOrPath = [&](StringView value, bool wave = false,
                                     bool midi = false) {
        String encoded(value);
        if (wave) encoded = encodePathWave(d, std::move(encoded));
        else if (midi) encoded = encodePathMidi(d, std::move(encoded));
        text(encoded);
    };

    switch (type) {
    case types::CType::Start:
    case types::CType::EndBadEnd:
    case types::CType::ElapseTime:
    case types::CType::ShowParty:
    case types::CType::HideParty:
    case types::CType::EffectBreak:
    case types::CType::BranchArea:
    case types::CType::BranchBattle:
    case types::CType::BranchIsBattle:
    case types::CType::Redisplay:
        break;
    case types::CType::LinkStart:
        text(content.start());
        break;
    case types::CType::StartBattle:
        writeCardId(content.battle());
        break;
    case types::CType::End:
        writeBool(f, content.complete());
        break;
    case types::CType::ChangeArea:
        id(content.area());
        break;
    case types::CType::TalkMessage: {
        String path;
        // Dの画像取得は新しいコピーを返す。最初のネイティブ画像を
        // 読む間は、一時値を参照するのではなく、その値を保持する。
        const auto images = content.cardImages();
        if (!images.empty() && images.front()) {
            const auto& image = *images.front();
            switch (image.type) {
            case types::CardImageType::File:
                path = encodePathTable(d, image.path());
                break;
            case types::CardImageType::Talker:
                path = "??" + types::fromTalker(image.talker());
                break;
            case types::CardImageType::PCNumber:
                // Classic/Nextは固定のPC番号の画像を表現できない。
                break;
            }
        }
        if (!extended) {
            writeString(f, path);
            writeString(f, utils::lastRet(content.text()), true);
        } else {
            Byte flags = path.empty() ? 0 : 1;
            if (content.selectionColumns() > 1) flags |= 0x80U;
            f.write(flags);
            if (!path.empty()) writeExString(f, path);
            writeExString(f, utils::lastRet(content.text()), true);
            if (content.selectionColumns() > 1) {
                f.writeExUInt(static_cast<std::int32_t>(content.selectionColumns()));
            }
        }
        break;
    }
    case types::CType::PlayBgm:
        writeTextOrPath(content.bgmPath(), false, true);
        break;
    case types::CType::ChangeBgImage:
        writeBgImages(d, f, imagePaths());
        break;
    case types::CType::PlaySound:
        writeTextOrPath(content.soundPath(), true);
        break;
    case types::CType::Wait:
        signedValue(content.wait());
        break;
    case types::CType::Effect: {
        signedValue(content.signedLevel());
        f.write(extended ? ::cwxeditor::cwx::cwl::fromRangeEffectContent(content.range())
                         : ::cwxeditor::cwx::cwl::fromRange(content.range()));
        f.write(::cwxeditor::cwx::cwl::fromEffectType(content.effectType()));
        f.write(::cwxeditor::cwx::cwl::fromResist(content.resist()));
        signedValue(content.successRate());
        if (extended) {
            f.write(Byte{1});
            writeExString(f, content.soundPath().empty()
                                  ? StringView("（なし）")
                                  : StringView(encodePathWave(d, content.soundPath())));
        } else {
            writeString(f, content.soundPath().empty()
                                   ? StringView("（なし）")
                                   : StringView(encodePathWave(d, content.soundPath())));
        }
        f.write(::cwxeditor::cwx::cwl::fromCardVisual(content.cardVisual()));
        const auto& motions = content.motionsRef();
        count(static_cast<std::uint32_t>(motions.size()));
        for (const auto& motion : motions) if (motion) writeMotion(d, f, *motion);
        break;
    }
    case types::CType::BranchSelect:
        writeBool(f, content.targetAll());
        writeBool(f, content.selectionMethod() == types::SelectionMethod::Random);
        break;
    case types::CType::BranchAbility:
        signedValue(content.signedLevel());
        f.write(::cwxeditor::cwx::cwl::fromTargetA(content.targetS()));
        if (extended) {
            f.writeExInt(static_cast<std::int32_t>(::cwxeditor::cwx::cwl::fromPhysical(content.physical())));
            f.writeExInt(::cwxeditor::cwx::cwl::fromMental(content.mental()));
        } else {
            f.writeL(::cwxeditor::cwx::cwl::fromPhysical(content.physical()));
            f.writeL(::cwxeditor::cwx::cwl::fromMental(content.mental()));
        }
        break;
    case types::CType::BranchRandom:
        if (extended) f.writeExUInt(content.percent()); else f.writeL(static_cast<std::uint32_t>(content.percent()));
        break;
    case types::CType::BranchFlag:
        text(content.flag());
        break;
    case types::CType::SetFlag:
        text(content.flag()); writeBool(f, content.flagValue());
        break;
    case types::CType::BranchMultiStep:
        text(content.step());
        break;
    case types::CType::SetStep:
    case types::CType::BranchStep:
        text(content.step());
        if (extended) f.writeExInt(content.stepValue());
        else f.writeL(static_cast<std::uint32_t>(std::min(content.stepValue(), 9)));
        break;
    case types::CType::BranchCast:
    case types::CType::GetCast:
    case types::CType::LoseCast:
        id(content.casts());
        break;
    case types::CType::BranchItem:
    case types::CType::GetItem:
    case types::CType::LoseItem:
        id(content.item()); count(static_cast<std::uint32_t>(content.cardNumber()));
        writeRange(content.range());
        break;
    case types::CType::BranchSkill:
    case types::CType::GetSkill:
    case types::CType::LoseSkill:
        id(content.skill()); count(static_cast<std::uint32_t>(content.cardNumber()));
        writeRange(content.range());
        break;
    case types::CType::BranchInfo:
    case types::CType::GetInfo:
    case types::CType::LoseInfo:
        id(content.info());
        break;
    case types::CType::BranchBeast:
    case types::CType::GetBeast:
    case types::CType::LoseBeast:
        id(content.beast()); count(static_cast<std::uint32_t>(content.cardNumber()));
        writeRange(content.range());
        break;
    case types::CType::BranchMoney:
    case types::CType::GetMoney:
    case types::CType::LoseMoney:
        if (extended) f.writeExUInt(content.money()); else f.writeL(static_cast<std::uint32_t>(content.money()));
        break;
    case types::CType::BranchCoupon:
        text(content.couponNames().empty() ? StringView{} : StringView(content.couponNames().front()));
        if (extended) { f.writeExInt(0); f.write(::cwxeditor::cwx::cwl::fromCouponRange(content.range())); writeBool(f, content.invertResult()); }
        else { f.writeL(0); f.write(::cwxeditor::cwx::cwl::fromCouponRange(content.range())); }
        break;
    case types::CType::GetCoupon:
        text(content.coupon()); signedValue(content.couponValue()); writeRange(content.range());
        if (extended) writeBool(f, false);
        break;
    case types::CType::LoseCoupon:
        text(content.coupon()); signedValue(0); writeRange(content.range());
        if (extended) writeBool(f, false);
        break;
    case types::CType::TalkDialog: {
        switch (content.talkerNC()) {
        case types::Talker::Selected: f.write(Byte{0}); break;
        case types::Talker::Random: f.write(Byte{1}); break;
        case types::Talker::Unselected: f.write(Byte{2}); break;
        case types::Talker::Valued: f.write(Byte{3}); break;
        default: throw summary::SummaryException("Unknown dialog talker");
        }
        const bool valued = content.talkerNC() == types::Talker::Valued;
        lib::Array<std::pair<String, std::int32_t>> coupons;
        if (valued) {
            std::unordered_set<String> saved_names;
            for (const auto& raw : content.coupons()) {
                if (!raw) continue;
                const auto saved_name = sjis::touni(sjis::tosjis(raw->name()));
                if (saved_names.insert(saved_name).second)
                    coupons.emplace_back(raw->name(), raw->value());
            }
        }
        if (valued) {
            if (extended) f.writeExUInt(static_cast<std::int32_t>(coupons.size() + (content.initValue() ? 1 : 0)));
            else f.writeL(static_cast<std::uint32_t>(coupons.size() + (content.initValue() ? 1 : 0)));
            if (content.initValue()) {
                if (extended) writeExString(f, ""); else writeString(f, "");
                if (extended) f.writeExInt(content.initValue()); else f.writeL(content.initValue());
            }
            for (const auto& [name, value] : coupons) {
                if (extended) writeExString(f, name); else writeString(f, name);
                if (extended) f.writeExInt(value); else f.writeL(value);
            }
        }
        if (extended) {
            f.writeExUInt(static_cast<std::int32_t>(content.dialogs().size()));
            for (const auto& dialog : content.dialogs()) {
                writeExStrings(f, dialog ? dialog->rCoupons() : lib::Array<String>{});
                writeExString(f, dialog ? utils::lastRet(dialog->text()) : String{}, true);
            }
            Byte options = content.selectTalker() ? 1 : 0;
            if (content.selectionColumns() > 1) options |= 2;
            f.write(options);
            if (content.selectionColumns() > 1) f.write(static_cast<Byte>(content.selectionColumns()));
        } else {
            f.writeL(static_cast<std::uint32_t>(content.dialogs().size()));
            for (const auto& dialog : content.dialogs()) {
                writeStrings(f, dialog ? dialog->rCoupons() : lib::Array<String>{});
                writeString(f, dialog ? utils::lastRet(dialog->text()) : String{}, true);
            }
        }
        break;
    }
    case types::CType::SetStepUp:
    case types::CType::SetStepDown:
    case types::CType::ReverseFlag:
        text(type == types::CType::ReverseFlag ? content.flag() : content.step());
        break;
    case types::CType::BranchLevel:
        writeBool(f, content.average());
        if (extended) f.writeExUInt(content.unsignedLevel()); else f.writeL(static_cast<std::uint32_t>(content.unsignedLevel()));
        break;
    case types::CType::BranchStatus:
        f.write(::cwxeditor::cwx::cwl::fromStatus(content.status())); writeRange(content.range());
        break;
    case types::CType::BranchPartyNumber:
        if (extended) f.writeExUInt(content.partyNumber()); else f.writeL(static_cast<std::uint32_t>(content.partyNumber()));
        break;
    case types::CType::CallStart:
        text(content.start());
        break;
    case types::CType::LinkPackage:
    case types::CType::CallPackage:
        id(content.packages());
        break;
    case types::CType::BranchCompleteStamp:
    case types::CType::GetCompleteStamp:
    case types::CType::LoseCompleteStamp:
        text(content.completeStamp());
        break;
    case types::CType::BranchGossip:
    case types::CType::GetGossip:
    case types::CType::LoseGossip:
        text(content.gossip());
        break;
    case types::CType::CheckFlag:
        text(content.flag());
        break;
    case types::CType::SubstituteStep:
    case types::CType::BranchStepCmp:
        text(content.step()); text(content.step2());
        break;
    case types::CType::SubstituteFlag:
    case types::CType::BranchFlagCmp:
        text(content.flag()); text(content.flag2());
        break;
    case types::CType::BranchRandomSelect: {
        f.write(::cwxeditor::cwx::cwl::fromCastRanges(content.castRange()));
        Byte style = 0;
        if (content.levelMax() > 0) style |= 1;
        if (content.status() != types::Status::None) style |= 2;
        f.write(style);
        if (style & 1) {
            if (extended) { f.writeExUInt(content.levelMin()); f.writeExUInt(content.levelMax()); }
            else { f.writeL(static_cast<std::uint32_t>(content.levelMin())); f.writeL(static_cast<std::uint32_t>(content.levelMax())); }
        }
        if (style & 2) f.write(::cwxeditor::cwx::cwl::fromStatus(content.status()));
        break;
    }
    case types::CType::BranchKeyCode: {
        f.write(::cwxeditor::cwx::cwl::fromKeyCodeRange(content.keyCodeRange()));
        const auto target = content.targetIsSkill() && content.targetIsItem() && content.targetIsBeast()
                                ? types::EffectCardType::All
                                : content.targetIsSkill() ? types::EffectCardType::Skill
                                : content.targetIsItem() ? types::EffectCardType::Item
                                : content.targetIsBeast() ? types::EffectCardType::Beast
                                : types::EffectCardType::All;
        f.write(::cwxeditor::cwx::cwl::fromEffectCardType(target));
        text(content.keyCode());
        break;
    }
    case types::CType::CheckStep:
        text(content.step());
        if (extended) f.writeExInt(content.stepValue()); else f.writeL(static_cast<std::uint32_t>(std::min(content.stepValue(), 9)));
        f.write(::cwxeditor::cwx::cwl::fromComparison4(content.comparison4()));
        break;
    case types::CType::BranchRound:
        f.write(::cwxeditor::cwx::cwl::fromComparison3(content.comparison3()));
        if (extended) f.writeExUInt(content.round()); else f.writeL(content.round());
        break;
    case types::CType::MoveBgImage:
        if (!extended) { writeString(f, ""); writeString(f, unsupported_message(), true); break; }
        writeExString(f, content.cellName());
        {
            Byte control = 0;
            if (content.positionType() != types::CoordinateType::None) control |= 1;
            if (content.sizeType() != types::CoordinateType::None) control |= 2;
            f.write(control);
            if (control & 1) {
                f.write(::cwxeditor::cwx::cwl::fromCoordinateType(content.positionType()));
                f.writeExInt(content.x()); f.writeExInt(content.y());
            }
            if (control & 2) {
                f.write(::cwxeditor::cwx::cwl::fromCoordinateType(content.sizeType()));
                f.writeExInt(content.width()); f.writeExInt(content.height());
            }
        }
        break;
    case types::CType::LoseBgImage:
        if (!extended) { writeString(f, ""); writeString(f, unsupported_message(), true); break; }
        writeExString(f, content.cellName());
        break;
    case types::CType::ReplaceBgImage:
        if (!extended) { writeString(f, ""); writeString(f, unsupported_message(), true); break; }
        writeExString(f, content.cellName()); writeBgImages(d, f, imagePaths(), true);
        break;
    case types::CType::MoveCard:
        if (!extended) { writeString(f, ""); writeString(f, unsupported_message(), true); break; }
        writeExString(f, content.cardGroup());
        writeBool(f, content.cardSpeed() <= 1);
        if (content.positionType() == types::CoordinateType::None) writeBool(f, false);
        else {
            writeBool(f, true);
            f.write(::cwxeditor::cwx::cwl::fromCoordinateType(content.positionType()));
            f.writeExInt(content.x()); f.writeExInt(content.y());
        }
        break;
    case types::CType::CustomXmlContent:
        text(content.xmlType()); text(content.xmlStr(), extended);
        if (extended) f.writeExUInt(content.xmlAssignment()); else f.writeL(static_cast<std::uint32_t>(content.xmlAssignment()));
        text(content.xmlVariable());
        break;
    case types::CType::ChangeEnvironment: {
        if (!extended) { writeString(f, ""); writeString(f, unsupported_message(), true); break; }
        Byte flags = content.backpackEnabled() == types::EnvironmentStatus::Disable ? 1 : 0;
        f.write(flags);
        break;
    }
    case types::CType::BranchMultiCoupon:
    case types::CType::BranchMultiRandom:
    case types::CType::BranchVariant:
    case types::CType::SetVariant:
    case types::CType::CheckVariant:
        if (extended) { f.write(Byte{0}); writeExString(f, unsupported_message(), true); }
        else { writeString(f, ""); writeString(f, unsupported_message(), true); }
        break;
    }
}

}  // namespace

void writeContent(SData& d, ByteIO& f, const Content& content) {
    const auto version = saveVersion(d);
    std::vector<const Content*> unarySpine;
    const Content* current = &content;
    while (current) {
        const auto type = current->type();
        f.write(contentTypeByte(type, version));
        // コンテント名はWSNレコードも含め、Classic
        // の外枠内では常に従来の文字列形式を使う。
        writeString(f, current->name());
        const auto children = current->detail().owner
                                  ? current->next()
                                  : lib::Array<event::ContentPtr>{};
        const auto encodedChildren = static_cast<std::uint32_t>(children.size()) +
                                     static_cast<std::uint32_t>(version >= 7 ? 70000U : 40000U);
        f.writeL(encodedChildren);
        unarySpine.push_back(current);

        // ネイティブ書き込み側のnullの子を許容する動作を維持し、
        // 実際の子レコードにはDの単一子の連鎖をたどる経路を使う。
        if (children.size() == 1U && children.front()) {
            current = children.front().get();
            continue;
        }
        if (children.size() > 1U) {
            for (const auto& child : children) {
                if (child) writeContent(d, f, *child);
            }
        }
        break;
    }

    // DのwriteContentの配置に合わせる。単一子の連鎖は反復で
    // 処理し、分岐の子を先に完了してから、各連鎖の内容を逆順に出力する。
    for (auto record = unarySpine.rbegin(); record != unarySpine.rend(); ++record) {
        writeContentPayload(d, f, **record, version);
    }
}

void writeCEventTree(SData& d, ByteIO& f, const EventTree& tree) {
    const auto version = saveVersion(d);
    const auto starts = static_cast<std::uint32_t>(tree.starts().size());
    f.writeL(starts + static_cast<std::uint32_t>(version >= 7 ? 0x07000000U : 0U));
    for (const auto& start : tree.starts()) {
        if (start) writeContent(d, f, *start);
    }
}

void writeEventTree(SData& d, ByteIO& f, const EventTree& tree) {
    const auto version = saveVersion(d);
    lib::Array<std::int32_t> ignitions;
    if (tree.fireEnter()) ignitions.push_back(1);
    if (tree.fireEscape()) ignitions.push_back(2);
    if (tree.fireLose()) ignitions.push_back(3);
    if (tree.fireEveryRound()) ignitions.push_back(4);
    if (tree.fireRound0()) ignitions.push_back(5);
    for (const auto round : tree.rounds()) {
        ignitions.push_back(-static_cast<std::int32_t>(round));
    }
    const auto starts = static_cast<std::uint32_t>(tree.starts().size());
    f.writeL(starts + static_cast<std::uint32_t>(version >= 7 ? 0x07000000U : 0U));
    for (const auto& start : tree.starts()) {
        if (start) writeContent(d, f, *start);
    }
    writeClassicCount(f, static_cast<std::uint32_t>(ignitions.size()), version);
    for (const auto ignition : ignitions) {
        if (version >= 7) f.writeExUInt(static_cast<std::uint32_t>(ignition));
        else f.writeL(ignition);
    }
    lib::Array<String> keys;
    keys.reserve(tree.keyCodes().size());
    for (const auto& key : tree.keyCodes()) {
        const system::System defaults;
        String value = (d.prop ? d.prop->sys() : defaults).convFireKeyCode(key);
        keys.push_back(std::move(value));
    }
    if (version >= 7) {
        writeBool(f, tree.keyCodeMatchingType() == types::MatchingType::And);
        writeExStrings(f, keys);
    } else {
        if (tree.keyCodeMatchingType() == types::MatchingType::And) {
            keys.insert(keys.begin(), "MatchingType=All");
        }
        writeStrings(f, keys);
    }
}

void writeBgImage(SData& d, ByteIO& f, const BgImage& image) {
    // Dの独立したサブクラス分岐と同様に、未知の公開拡張ではClassic
    // のレコードを書かず、WSN.7では共通ヘッダーだけを書く。Dが
    // ここで受け付ける拡張に、勝手な内容を作ったり拒否したりしない。
    const auto version = saveVersion(d);
    if (version < 7 &&
        !dynamic_cast<const background::ImageCell*>(&image) &&
        !dynamic_cast<const background::TextCell*>(&image) &&
        !dynamic_cast<const background::ColorCell*>(&image) &&
        !dynamic_cast<const background::PCCell*>(&image)) return;
    f.writeL(image.x());
    f.writeL(image.y());
    f.writeL(static_cast<std::uint32_t>(image.width()) +
             static_cast<std::uint32_t>(version >= 7 ? 70000U :
                                        dynamic_cast<const background::ImageCell*>(&image)
                                            ? 40000U : 60000U));
    f.writeL(static_cast<std::uint32_t>(image.height()));
    if (version < 7) {
        if (const auto* cell = dynamic_cast<const background::ImageCell*>(&image)) {
            writeString(f, encodePathTable(d, cell->path()));
            writeBool(f, cell->mask());
            writeString(f, cell->flag());
            f.writeL(static_cast<std::int8_t>(0));
            return;
        }
        if (const auto* cell = dynamic_cast<const background::TextCell*>(&image)) {
            f.write(static_cast<Byte>(2)); writeBool(f, cell->mask());
            writeString(f, cell->text()); writeString(f, cell->fontName());
            f.writeL(cell->size());
            const auto color = cell->color();
            f.write(static_cast<Byte>(color.r)); f.write(static_cast<Byte>(color.g));
            f.write(static_cast<Byte>(color.b)); f.write(static_cast<Byte>(color.a));
            Byte style = 0;
            if (cell->bold()) style |= 1; if (cell->italic()) style |= 2;
            if (cell->underline()) style |= 4; if (cell->strike()) style |= 8;
            if (cell->borderingType() != types::BorderingType::None) style |= 0x10;
            if (cell->vertical()) style |= 0x20;
            f.write(style);
            if (style & 0x10) {
                f.write(::cwxeditor::cwx::cwl::fromBorderingType(cell->borderingType()));
                const auto border = cell->borderingColor();
                f.write(static_cast<Byte>(border.r)); f.write(static_cast<Byte>(border.g));
                f.write(static_cast<Byte>(border.b)); f.write(static_cast<Byte>(border.a));
                f.writeL(cell->borderingWidth());
            }
            f.write(static_cast<Byte>(100)); f.writeL(0U); f.writeL(0U);
            f.write(static_cast<Byte>(cell->vertical() ? 2 : 0));
            writeString(f, cell->flag()); f.writeL(static_cast<std::int8_t>(0));
            return;
        }
        if (const auto* cell = dynamic_cast<const background::ColorCell*>(&image)) {
            f.write(static_cast<Byte>(3));
            f.write(::cwxeditor::cwx::cwl::fromBlendMode(cell->blendMode(), cell->mask()));
            f.write(::cwxeditor::cwx::cwl::fromGradientDir(cell->gradientDir()));
            auto color = cell->color1();
            f.write(static_cast<Byte>(color.b)); f.write(static_cast<Byte>(color.g));
            f.write(static_cast<Byte>(color.r)); f.write(static_cast<Byte>(color.a));
            if (cell->gradientDir() != types::GradientDir::None) {
                color = cell->color2();
                f.write(static_cast<Byte>(color.b)); f.write(static_cast<Byte>(color.g));
                f.write(static_cast<Byte>(color.r)); f.write(static_cast<Byte>(color.a));
            }
            writeString(f, cell->flag()); f.writeL(static_cast<std::int8_t>(0));
            return;
        }
        if (const auto* cell = dynamic_cast<const background::PCCell*>(&image)) {
            f.write(static_cast<Byte>(4)); writeBool(f, cell->mask());
            f.write(static_cast<Byte>(cell->pcNumber())); writeString(f, cell->flag());
            f.writeL(static_cast<std::int8_t>(0));
            return;
        }
        return;
    }

    if (const auto* cell = dynamic_cast<const background::ImageCell*>(&image)) {
        f.write(static_cast<Byte>(0));
        writeBool(f, cell->mask());
        writeBool(f, cell->layer() > types::LAYER_MENU_CARD);
        writeBool(f, false);
        writeExString(f, encodePathTable(d, cell->path()));
        writeExString(f, cell->flag());
        f.write(static_cast<Byte>(0));
        writeExString(f, cell->cellName());
        return;
    }
    if (const auto* cell = dynamic_cast<const background::TextCell*>(&image)) {
        f.write(static_cast<Byte>(2)); writeBool(f, cell->mask());
        writeBool(f, cell->layer() > types::LAYER_MENU_CARD);
        writeExString(f, cell->text()); writeExString(f, cell->fontName());
        f.writeExUInt(static_cast<std::int32_t>(cell->size()));
        const auto color = cell->color();
        f.write(static_cast<Byte>(color.r)); f.write(static_cast<Byte>(color.g));
        f.write(static_cast<Byte>(color.b)); f.write(static_cast<Byte>(color.a));
        Byte style = 0;
        if (cell->bold()) style |= 1; if (cell->italic()) style |= 2;
        if (cell->underline()) style |= 4; if (cell->strike()) style |= 8;
        if (cell->borderingType() != types::BorderingType::None) style |= 0x10;
        if (cell->vertical()) style |= 0x20;
        f.write(style);
        if (style & 0x10) {
            f.write(::cwxeditor::cwx::cwl::fromBorderingType(cell->borderingType()));
            const auto border = cell->borderingColor();
            f.write(static_cast<Byte>(border.r)); f.write(static_cast<Byte>(border.g));
            f.write(static_cast<Byte>(border.b)); f.write(static_cast<Byte>(border.a));
            f.writeExUInt(static_cast<std::int32_t>(cell->borderingWidth()));
        }
        f.write(static_cast<Byte>(0xC8)); f.write(static_cast<Byte>(1));
        f.write(static_cast<Byte>(0)); f.write(static_cast<Byte>(0));
        f.write(static_cast<Byte>(cell->vertical() ? 2 : 0));
        writeExString(f, cell->flag()); f.write(static_cast<Byte>(0));
        writeExString(f, cell->cellName());
        return;
    }
    if (const auto* cell = dynamic_cast<const background::ColorCell*>(&image)) {
        f.write(static_cast<Byte>(3));
        f.write(::cwxeditor::cwx::cwl::fromBlendMode(cell->blendMode(), cell->mask()));
        writeBool(f, cell->layer() > types::LAYER_MENU_CARD);
        f.write(::cwxeditor::cwx::cwl::fromGradientDir(cell->gradientDir()));
        auto color = cell->color1();
        f.write(static_cast<Byte>(color.b)); f.write(static_cast<Byte>(color.g));
        f.write(static_cast<Byte>(color.r)); f.write(static_cast<Byte>(color.a));
        if (cell->gradientDir() != types::GradientDir::None) {
            color = cell->color2();
            f.write(static_cast<Byte>(color.b)); f.write(static_cast<Byte>(color.g));
            f.write(static_cast<Byte>(color.r)); f.write(static_cast<Byte>(color.a));
        }
        writeExString(f, cell->flag()); f.write(static_cast<Byte>(0));
        writeExString(f, cell->cellName());
        return;
    }
    if (const auto* cell = dynamic_cast<const background::PCCell*>(&image)) {
        f.write(static_cast<Byte>(4)); writeBool(f, cell->mask());
        writeBool(f, cell->layer() > types::LAYER_MENU_CARD);
        f.write(static_cast<Byte>(cell->pcNumber())); writeExString(f, cell->flag());
        f.write(static_cast<Byte>(0)); writeExString(f, cell->cellName());
        return;
    }
    // 既知の派生型に一致しない場合、DはWSN.7の共通ヘッダーをそのまま残す。
}

namespace {

template <typename CellArray>
CellArray convInheritCellsWImpl(const CellArray& input) {
    using CellPointer = typename CellArray::value_type;
    using Cell = typename CellPointer::element_type;
    using Image = std::conditional_t<std::is_const_v<Cell>,
                                     const background::ImageCell,
                                     background::ImageCell>;
    CellArray cells = input;
    if (cells.empty()) return cells;
    const auto image = std::dynamic_pointer_cast<Image>(cells.front());
    if (image && image->flag().empty() && image->x() == 0 && image->y() == 0 &&
        image->width() == 632 && image->height() == 420 && !image->mask() &&
        image->cellName().empty() && image->layer() > types::LAYER_MENU_CARD) {
        cells.insert(cells.begin(), std::make_shared<background::ImageCell>(
            "Black.bmp", String{}, 0, 0, 632, 420, false));
    }
    return cells;
}

}  // namespace

lib::Array<ConstOwnerBgImage> convInheritCellsW(
    const lib::Array<ConstOwnerBgImage>& input) {
    return convInheritCellsWImpl(input);
}

lib::Array<OwnerBgImage> convInheritCellsW(const lib::Array<OwnerBgImage>& input) {
    return convInheritCellsWImpl(input);
}

namespace {

template <typename CellArray>
void writeBgImagesImpl(SData& d, ByteIO& f, const CellArray& images,
                       bool replaceBackground) {
    const auto version = saveVersion(d);
    const auto cells = convInheritCellsWImpl(images);
    if (replaceBackground) {
        f.writeExUInt(static_cast<std::int32_t>(cells.size()));
    } else {
        bool direct = false;
        if (!cells.empty()) {
            using Cell = typename CellArray::value_type::element_type;
            using Image = std::conditional_t<std::is_const_v<Cell>,
                                             const background::ImageCell,
                                             background::ImageCell>;
            const auto image = std::dynamic_pointer_cast<Image>(cells.front());
            direct = image && !image->path().empty() && image->flag().empty() && image->x() == 0 &&
                     image->y() == 0 && image->width() == 632 && image->height() == 420 &&
                     !image->mask() && image->cellName().empty();
        }
        writeClassicCount(f, static_cast<std::uint32_t>(cells.size() + (direct ? 0 : 1)), version);
        if (!direct) {
            writeBgImage(d, f, *std::make_shared<background::ImageCell>(
                String{}, String{}, 0, 0, 632, 420, false));
        }
    }
    for (const auto& image : cells) if (image) writeBgImage(d, f, *image);
}

}  // namespace

void writeBgImages(SData& d, ByteIO& f,
                   const lib::Array<ConstOwnerBgImage>& images,
                   bool replaceBackground) {
    writeBgImagesImpl(d, f, images, replaceBackground);
}

void writeBgImages(SData& d, ByteIO& f, const lib::Array<OwnerBgImage>& images,
                   bool replaceBackground) {
    lib::Array<ConstOwnerBgImage> readOnly;
    readOnly.reserve(images.size());
    for (const auto& image : images) readOnly.push_back(image);
    writeBgImages(d, f, readOnly, replaceBackground);
}

namespace {

template <typename AreaT>
lib::Array<OwnerEventTree> classicRootEvents(const AreaT& area) {
    lib::Array<OwnerEventTree> result;
    // ルート検索は範囲検査を行う。配列外の番兵を参照しない。
    for (std::size_t index = 0; index < area.trees().size(); ++index) {
        auto tree = const_cast<AreaT&>(area).etFromPath({0U, index});
        if (!tree) break;
        result.push_back(std::move(tree));
    }
    return result;
}

void writeMenuCardRecord(SData& d, ByteIO& f, const area::MenuCard& card,
                         std::int32_t version) {
    const auto paths = card.paths();
    const auto first = paths.empty() ? lib::Owner<CardImage>{} : paths.front();
    const bool binaryImage = first && first->type == types::CardImageType::File &&
                             utils::isBinImg(first->path());
    if (version < 7) {
        f.writeL(static_cast<std::int8_t>(0));
        writeImage(d, f, {}, binaryImage ? StringView(first->path()) : StringView{});
        writeString(f, card.name());
        f.writeL(static_cast<std::int8_t>(0x40));
        f.writeL(static_cast<std::int8_t>(0x9C));
        f.writeL(static_cast<std::int8_t>(0));
        f.writeL(static_cast<std::int8_t>(0));
        writeString(f, card.desc());
        writeClassicCount(f, static_cast<std::uint32_t>(card.trees().size()), version);
        for (const auto& tree : card.trees()) if (tree) writeEventTree(d, f, *tree);
        writeString(f, card.flag());
        f.writeL(card.scale()); f.writeL(card.x()); f.writeL(card.y());
        if (first && first->type == types::CardImageType::File && !binaryImage) {
            writeString(f, encodePathTable(d, first->path()));
        } else if (first && first->type == types::CardImageType::PCNumber && first->pcNumber()) {
            writeString(f, std::to_string(first->pcNumber()));
        } else {
            writeString(f, "");
        }
        return;
    }
    f.write(static_cast<Byte>(0x80)); f.write(static_cast<Byte>(version)); f.write(Byte{0});
    writeImage(d, f, {}, binaryImage ? StringView(first->path()) : StringView{});
    writeExString(f, card.name()); f.write(Byte{0}); writeExString(f, card.desc());
    f.writeExUInt(static_cast<std::int32_t>(card.trees().size()));
    for (const auto& tree : card.trees()) if (tree) writeEventTree(d, f, *tree);
    writeExString(f, card.flag()); f.writeExUInt(static_cast<std::int32_t>(card.scale()));
    f.writeExInt(card.x()); f.writeExInt(card.y());
    Byte bits = 0;
    if (card.animationSpeed() <= 1) bits |= 0x10;
    if (card.expandSPChars()) bits |= 0x20;
    if (first && first->type == types::CardImageType::File && !binaryImage) {
        bits |= 1; f.write(bits); writeExString(f, encodePathTable(d, first->path()));
    } else if (first && first->type == types::CardImageType::PCNumber && first->pcNumber()) {
        bits |= 2; f.write(bits); f.write(static_cast<Byte>(first->pcNumber()));
    } else {
        f.write(bits);
    }
    writeExString(f, card.cardGroup());
}

}  // namespace

void writeArea(SData& d, ByteIO& f, const Area& area) {
    const auto version = saveVersion(d);
    const auto events = classicRootEvents(area);
    if (version < 7) {
        f.writeL(static_cast<std::int8_t>(0)); f.writeL(0U);
        writeString(f, area.name()); f.writeL(static_cast<std::uint32_t>(area.id() + 40000U));
    } else {
        f.write(Byte{0xFF}); f.write(static_cast<Byte>(version));
        writeExString(f, area.name()); f.writeExUInt(static_cast<std::int32_t>(area.id()));
    }
    writeClassicCount(f, static_cast<std::uint32_t>(events.size()), version);
    for (const auto& tree : events) if (tree) writeEventTree(d, f, *tree);
    writeBool(f, !area.spAuto());
    writeClassicCount(f, static_cast<std::uint32_t>(area.cards().size()), version);
    for (const auto& card : area.cards()) if (card) writeMenuCardRecord(d, f, *card, version);
    writeBgImages(d, f, snapshotBackgrounds(area.backs()));
}

void writeBattle(SData& d, ByteIO& f, const Battle& battle) {
    const auto version = saveVersion(d);
    const auto events = classicRootEvents(battle);
    if (version < 7) {
        f.writeL(static_cast<std::int8_t>(1)); f.writeL(0U);
        writeString(f, battle.name()); f.writeL(static_cast<std::uint32_t>(battle.id() + 40000U));
    } else {
        f.write(Byte{0xFF}); f.write(static_cast<Byte>(version));
        writeExString(f, battle.name()); f.writeExUInt(static_cast<std::int32_t>(battle.id()));
    }
    writeClassicCount(f, static_cast<std::uint32_t>(events.size()), version);
    for (const auto& tree : events) if (tree) writeEventTree(d, f, *tree);
    writeBool(f, !battle.spAuto());
    writeClassicCount(f, static_cast<std::uint32_t>(battle.cards().size()), version);
    for (const auto& card : battle.cards()) {
        if (!card) continue;
        writeClassicId(f, card->id(), version);
        f.writeL(static_cast<std::uint32_t>(card->trees().size()));
        for (const auto& tree : card->trees()) if (tree) writeEventTree(d, f, *tree);
        writeString(f, card->flag());
        f.writeL(static_cast<std::uint32_t>(card->scale()));
        f.writeL(card->x()); f.writeL(card->y());
        if (version >= 7) {
            Byte bits = 0;
            if (card->action(types::ActionCardType::RunAway)) bits |= 1;
            std::uint32_t pc = 0;
            if (card->isOverrideImage()) {
                const auto images = card->overrideImages();
                if (!images.empty() && images.front() &&
                    images.front()->type == types::CardImageType::PCNumber) {
                    pc = images.front()->pcNumber();
                }
            }
            if (pc) bits |= 2; if (card->isOverrideName()) bits |= 4;
            f.write(bits);
            if (pc) f.write(static_cast<Byte>(pc));
            if (card->isOverrideName()) writeExString(f, card->overrideName());
        } else {
            writeBool(f, card->action(types::ActionCardType::RunAway));
        }
    }
    if (version < 7) writeString(f, encodePathMidi(d, battle.music()));
    else writeExString(f, encodePathMidi(d, battle.music()));
}

void writePackage(SData& d, ByteIO& f, const Package& package) {
    const auto version = saveVersion(d);
    lib::Array<OwnerEventTree> events;
    for (std::size_t index = 0;; ++index) {
        auto tree = const_cast<Package&>(package).etFromPath({index});
        if (!tree) break;
        events.push_back(std::move(tree));
    }
    if (version < 7) {
        f.writeL(4U); writeString(f, package.name()); f.writeL(static_cast<std::uint32_t>(package.id()));
        f.writeL(static_cast<std::uint32_t>(events.size()));
    } else {
        f.writeL(7U); writeExString(f, package.name());
        f.writeExUInt(static_cast<std::int32_t>(package.id()));
        f.writeExUInt(static_cast<std::int32_t>(events.size()));
    }
    for (const auto& tree : events) if (tree) writeCEventTree(d, f, *tree);
}

namespace {

String classicCardImagePath(const card::Card& card) {
    const auto paths = card.paths();
    if (paths.empty() || !paths.front()) return {};
    const auto& image = paths.front();
    if (image->type != types::CardImageType::File) return {};
    return image->path();
}

}  // namespace

void writeCast(SData& d, ByteIO& f, const CastCard& card) {
    const auto version = saveVersion(d);
    const auto image = classicCardImagePath(card);
    auto& mutableCast = const_cast<CastCard&>(card);
    lib::Array<lib::Owner<coupon::Coupon>> coupons;
    std::unordered_set<String> couponNames;
    for (const auto& coupon : mutableCast.coupons()) {
        if (coupon && couponNames.insert(sjis::touni(sjis::tosjis(coupon->name()))).second)
            coupons.push_back(coupon);
    }
    const auto items = mutableCast.items();
    const auto skills = mutableCast.skills();
    const auto beasts = mutableCast.beasts();
    if (version < 7) {
        f.writeL(static_cast<std::int8_t>(2));
        writeImage(d, f, {}, image);
        writeString(f, card.name()); f.writeL(static_cast<std::uint32_t>(card.id() + 40000U));
        writeBool(f, card.weaponResist()); writeBool(f, card.magicResist());
        writeBool(f, card.undead()); writeBool(f, card.automaton());
        writeBool(f, card.unholy()); writeBool(f, card.constructure());
        writeBool(f, card.resist(types::Element::Fire)); writeBool(f, card.resist(types::Element::Ice));
        writeBool(f, card.weakness(types::Element::Fire)); writeBool(f, card.weakness(types::Element::Ice));
        f.writeL(card.level()); f.writeL(0U); writeString(f, card.desc(), true, true);
        f.writeL(card.life()); f.writeL(card.lifeMax()); f.writeL(card.paralyze()); f.writeL(card.poison());
        f.writeL(static_cast<std::int32_t>(card.defaultEnhance(types::Enhance::Avoid)));
        f.writeL(static_cast<std::int32_t>(card.defaultEnhance(types::Enhance::Resist)));
        f.writeL(static_cast<std::int32_t>(card.defaultEnhance(types::Enhance::Defense)));
        for (const auto p : {types::Physical::Dex, types::Physical::Agl, types::Physical::Int,
                             types::Physical::Str, types::Physical::Vit, types::Physical::Min}) {
            f.writeL(card.physical(p));
        }
        for (const auto m : {types::Mental::Aggressive, types::Mental::Cheerful,
                             types::Mental::Brave, types::Mental::Cautious, types::Mental::Trickish}) {
            f.writeL(static_cast<std::int32_t>(card.mental(m)));
        }
        f.write(::cwxeditor::cwx::cwl::fromMentality(card.mentality()));
        f.writeL(card.mentalityRound()); f.writeL(card.bindRound()); f.writeL(card.silenceRound());
        f.writeL(card.faceUpRound()); f.writeL(card.antiMagicRound());
        for (const auto enhance : {types::Enhance::Action, types::Enhance::Avoid,
                                   types::Enhance::Resist, types::Enhance::Defense}) {
            auto value = card.enhance(enhance);
            auto round = value ? card.enhanceRound(enhance) : 0U;
            f.writeL(value); f.writeL(round);
        }
        f.writeL(static_cast<std::uint32_t>(items.size()));
        for (const auto& item : items) if (item) writeItem(d, f, *item);
        f.writeL(static_cast<std::uint32_t>(skills.size()));
        for (const auto& skill : skills) if (skill) writeSkill(d, f, *skill);
        f.writeL(static_cast<std::uint32_t>(beasts.size()));
        for (const auto& beast : beasts) if (beast) writeBeast(d, f, *beast);
        f.writeL(static_cast<std::uint32_t>(coupons.size()));
        for (const auto& coupon : coupons) {
            if (!coupon) continue;
            writeString(f, coupon->name()); f.writeL(coupon->value());
        }
        return;
    }
    f.write(Byte{0x80}); f.write(static_cast<Byte>(version)); f.write(Byte{2});
    writeImage(d, f, {}, image); writeExString(f, card.name());
    f.writeExUInt(static_cast<std::int32_t>(card.id()));
    Byte race1 = 0, race2 = 0;
    if (card.weaponResist()) race1 |= 1; if (card.magicResist()) race1 |= 2;
    if (card.undead()) race1 |= 4; if (card.automaton()) race1 |= 8;
    if (card.unholy()) race1 |= 16; if (card.constructure()) race1 |= 32;
    if (card.resist(types::Element::Fire)) race1 |= 64; if (card.resist(types::Element::Ice)) race1 |= 128;
    if (card.weakness(types::Element::Fire)) race2 |= 1; if (card.weakness(types::Element::Ice)) race2 |= 2;
    f.write(race1); f.write(race2); f.writeExUInt(static_cast<std::int32_t>(card.level()));
    writeExString(f, card.desc(), true, true); f.writeExUInt(static_cast<std::int32_t>(card.life()));
    f.writeExUInt(static_cast<std::int32_t>(card.lifeMax())); f.writeExUInt(static_cast<std::int32_t>(card.paralyze()));
    f.writeExUInt(static_cast<std::int32_t>(card.poison()));
    f.writeExInt(static_cast<std::int32_t>(card.defaultEnhance(types::Enhance::Avoid)));
    f.writeExInt(static_cast<std::int32_t>(card.defaultEnhance(types::Enhance::Resist)));
    f.writeExInt(static_cast<std::int32_t>(card.defaultEnhance(types::Enhance::Defense)));
    for (const auto p : {types::Physical::Dex, types::Physical::Agl, types::Physical::Int,
                         types::Physical::Str, types::Physical::Vit, types::Physical::Min}) {
        f.writeExUInt(static_cast<std::int32_t>(card.physical(p)));
    }
    for (const auto m : {types::Mental::Aggressive, types::Mental::Cheerful,
                         types::Mental::Brave, types::Mental::Cautious, types::Mental::Trickish}) {
        f.writeExInt(static_cast<std::int32_t>(card.mental(m)));
    }
    f.write(::cwxeditor::cwx::cwl::fromMentality(card.mentality()));
    f.writeExUInt(static_cast<std::int32_t>(card.mentalityRound()));
    f.writeExUInt(static_cast<std::int32_t>(card.bindRound()));
    f.writeExUInt(static_cast<std::int32_t>(card.silenceRound()));
    f.writeExUInt(static_cast<std::int32_t>(card.faceUpRound()));
    f.writeExUInt(static_cast<std::int32_t>(card.antiMagicRound()));
    for (const auto enhance : {types::Enhance::Action, types::Enhance::Avoid,
                               types::Enhance::Resist, types::Enhance::Defense}) {
        auto value = card.enhance(enhance);
        auto round = value ? card.enhanceRound(enhance) : 0U;
        f.writeExInt(value); f.writeExUInt(static_cast<std::int32_t>(round));
    }
    f.writeExUInt(static_cast<std::int32_t>(items.size()));
    for (const auto& item : items) if (item) writeItem(d, f, *item);
    f.writeExUInt(static_cast<std::int32_t>(skills.size()));
    for (const auto& skill : skills) if (skill) writeSkill(d, f, *skill);
    f.writeExUInt(static_cast<std::int32_t>(beasts.size()));
    for (const auto& beast : beasts) if (beast) writeBeast(d, f, *beast);
    f.write(Byte{0}); f.writeExUInt(static_cast<std::int32_t>(coupons.size()));
    for (const auto& coupon : coupons) {
        if (!coupon) continue;
        writeString(f, coupon->name()); f.writeL(coupon->value());
    }
}

void writeEffCard(SData& d, ByteIO& f, const EffectCard& card, Byte type,
                  std::uint64_t id) {
    const auto version = saveVersion(d);
    const auto image = classicCardImagePath(card);
    const auto motions = card.motions();
    const auto keys = card.keyCodes();
    if (version < 7) {
        f.write(type); writeImage(d, f, {}, image); writeString(f, card.name());
        f.writeL(static_cast<std::uint32_t>(id + 40000U)); writeString(f, card.desc());
        f.writeL(static_cast<std::uint32_t>(::cwxeditor::cwx::cwl::fromPhysical(card.physical())));
        f.writeL(::cwxeditor::cwx::cwl::fromMental(card.mental())); writeBool(f, card.spell());
        writeBool(f, card.allRange()); f.write(::cwxeditor::cwx::cwl::fromCardTarget(card.target()));
        f.write(::cwxeditor::cwx::cwl::fromEffectType(card.effectType()));
        f.write(::cwxeditor::cwx::cwl::fromResist(card.resist())); f.writeL(card.successRate());
        f.write(::cwxeditor::cwx::cwl::fromCardVisual(card.visual()));
        f.writeL(static_cast<std::uint32_t>(motions.size()));
        for (const auto& motion : motions) if (motion) writeMotion(d, f, *motion);
        f.writeL(card.enhance(types::Enhance::Avoid)); f.writeL(card.enhance(types::Enhance::Resist));
        f.writeL(card.enhance(types::Enhance::Defense));
        writeString(f, card.soundPath1().empty() ? StringView("（なし）") : StringView(encodePathWave(d, card.soundPath1())));
        writeString(f, card.soundPath2().empty() ? StringView("（なし）") : StringView(encodePathWave(d, card.soundPath2())));
        for (std::size_t i = 0; i < 5; ++i) writeString(f, i < keys.size() ? keys[i] : StringView{});
        if (version >= 2) f.write(::cwxeditor::cwx::cwl::fromPremium(card.premium()));
        if (version > 2) {
            writeString(f, card.scenario()); writeString(f, card.author());
            f.writeL(static_cast<std::uint32_t>(card.trees().size()));
            for (const auto& tree : card.trees()) if (tree) writeCEventTree(d, f, *tree);
        }
        return;
    }
    f.write(Byte{0x80}); f.write(static_cast<Byte>(version)); f.write(type);
    writeImage(d, f, {}, image); writeExString(f, card.name());
    f.writeExUInt(static_cast<std::int32_t>(id)); writeExString(f, card.desc());
    f.writeExInt(static_cast<std::int32_t>(::cwxeditor::cwx::cwl::fromPhysical(card.physical())));
    f.writeExInt(::cwxeditor::cwx::cwl::fromMental(card.mental())); writeBool(f, card.spell());
    writeBool(f, card.allRange()); f.write(::cwxeditor::cwx::cwl::fromCardTarget(card.target()));
    f.write(::cwxeditor::cwx::cwl::fromEffectType(card.effectType()));
    f.write(::cwxeditor::cwx::cwl::fromResist(card.resist())); f.writeExInt(card.successRate());
    f.write(::cwxeditor::cwx::cwl::fromCardVisual(card.visual()));
    f.writeExUInt(static_cast<std::int32_t>(motions.size()));
    for (const auto& motion : motions) if (motion) writeMotion(d, f, *motion);
    f.writeExInt(card.enhance(types::Enhance::Avoid)); f.writeExInt(card.enhance(types::Enhance::Resist));
    f.writeExInt(card.enhance(types::Enhance::Defense));
    f.write(Byte{1}); writeExString(f, card.soundPath1().empty() ? StringView("（なし）") : StringView(encodePathWave(d, card.soundPath1())));
    f.write(Byte{1}); writeExString(f, card.soundPath2().empty() ? StringView("（なし）") : StringView(encodePathWave(d, card.soundPath2())));
    f.writeExUInt(static_cast<std::int32_t>(keys.size()));
    for (const auto& key : keys) writeExString(f, key);
    f.write(::cwxeditor::cwx::cwl::fromPremium(card.premium())); f.write(Byte{0});
    writeExString(f, card.scenario()); writeExString(f, card.author());
    f.writeExUInt(static_cast<std::int32_t>(card.trees().size()));
    for (const auto& tree : card.trees()) if (tree) writeCEventTree(d, f, *tree);
}

void writeSkill(SData& d, ByteIO& f, const SkillCard& card) {
    const auto id = card.id();
    const auto hold = card.hold();
    lib::Owner<SkillCard> resolved;
    lib::Owner<SkillCard> fallback;
    const SkillCard* source = &card;
    if (card.linkId()) {
        if (d.skill) resolved = d.skill(card.linkId());
        if (resolved) {
            source = resolved.get();
        } else {
            fallback = std::make_shared<SkillCard>(
                nullptr, id, String{}, lib::Array<lib::Owner<CardImage>>{}, String{});
            source = fallback.get();
        }
    }
    if (card.linkId()) {
        const auto path = const_cast<SkillCard&>(card).cwxPath(true);
        if (!path.empty()) d.cardRef[path] = card.linkId();
    }
    writeEffCard(d, f, *source, Byte{0x5}, id);
    writeBool(f, hold);
    const auto version = saveVersion(d);
    if (version < 7) { f.writeL(source->level()); f.writeL(source->useLimit()); }
    else { f.writeExInt(static_cast<std::int32_t>(source->level())); f.writeExUInt(static_cast<std::int32_t>(source->useLimit())); writeBool(f, true); }
}

void writeItem(SData& d, ByteIO& f, const ItemCard& card) {
    const auto id = card.id();
    const auto hold = card.hold();
    lib::Owner<ItemCard> resolved;
    lib::Owner<ItemCard> fallback;
    const ItemCard* source = &card;
    if (card.linkId()) {
        if (d.item) resolved = d.item(card.linkId());
        if (resolved) {
            source = resolved.get();
        } else {
            fallback = std::make_shared<ItemCard>(
                nullptr, id, String{}, lib::Array<lib::Owner<CardImage>>{}, String{});
            source = fallback.get();
        }
    }
    if (card.linkId()) {
        const auto path = const_cast<ItemCard&>(card).cwxPath(true);
        if (!path.empty()) d.cardRef[path] = card.linkId();
    }
    writeEffCard(d, f, *source, Byte{0x3}, id); writeBool(f, hold);
    const auto version = saveVersion(d);
    if (version < 7) {
        f.writeL(source->useLimit()); f.writeL(source->useLimitMax()); f.writeL(source->price());
        f.writeL(source->enhanceOwner(types::Enhance::Avoid)); f.writeL(source->enhanceOwner(types::Enhance::Resist)); f.writeL(source->enhanceOwner(types::Enhance::Defense));
    } else {
        f.writeExUInt(static_cast<std::int32_t>(source->useLimit())); f.writeExUInt(static_cast<std::int32_t>(source->useLimitMax())); f.writeExUInt(static_cast<std::int32_t>(source->price()));
        f.writeExInt(source->enhanceOwner(types::Enhance::Avoid)); f.writeExInt(source->enhanceOwner(types::Enhance::Resist)); f.writeExInt(source->enhanceOwner(types::Enhance::Defense));
    }
}

void writeBeast(SData& d, ByteIO& f, const BeastCard& card) {
    const auto id = card.id();
    lib::Owner<BeastCard> resolved;
    lib::Owner<BeastCard> fallback;
    const BeastCard* source = &card;
    if (card.linkId()) {
        if (d.beast) resolved = d.beast(card.linkId());
        if (resolved) {
            source = resolved.get();
        } else {
            fallback = std::make_shared<BeastCard>(
                nullptr, id, String{}, lib::Array<lib::Owner<CardImage>>{}, String{});
            source = fallback.get();
        }
    }
    if (card.linkId()) {
        const auto path = const_cast<BeastCard&>(card).cwxPath(true);
        if (!path.empty()) d.cardRef[path] = card.linkId();
    }
    writeEffCard(d, f, *source, Byte{0x6}, id); writeBool(f, false);
    const auto version = saveVersion(d);
    if (version < 7) f.writeL(source->useLimit());
    else { f.writeExUInt(static_cast<std::int32_t>(source->useLimit())); f.write(static_cast<Byte>(source->useLimit() ? 1 : 0)); }
}

void writeInfo(SData& d, ByteIO& f, const InfoCard& card) {
    const auto version = saveVersion(d);
    const auto image = classicCardImagePath(card);
    if (version < 7) {
        f.writeL(static_cast<std::int8_t>(4)); writeImage(d, f, {}, image);
        writeString(f, card.name()); f.writeL(static_cast<std::uint32_t>(card.id() + 40000U));
        writeString(f, card.desc());
    } else {
        f.write(Byte{0x80}); f.write(static_cast<Byte>(version)); f.write(Byte{4});
        writeImage(d, f, {}, image); writeExString(f, card.name());
        f.writeExUInt(static_cast<std::int32_t>(card.id())); writeExString(f, card.desc());
    }
}

void saveLScenario(const OwnerSummary& summary, StringView scenarioPath,
                   const Skin& skin, const CProps& prop, const SaveOption& opt,
                   FileSync* sync) {
    perf::TraceScope trace(__FILE__, __LINE__);
    if (!summary) throw std::invalid_argument("Cannot save an empty summary");
    const auto root = fsPath(scenarioPath);
    std::filesystem::create_directories(root);

    const auto isClassicResource = [](StringView name) {
        const auto ext = name.size() >= 4 ? name.substr(name.size() - 4) : StringView{};
        if (ext == ".wex") {
            return name == "Comment.wex" || name == "ImageRef.wex" ||
                   name == "CardRef.wex" || name == "Template.wex";
        }
        if (ext != ".wid") return false;
        const std::array<StringView, 8> prefixes{
            "Area", "Battle", "Package", "Mate", "Skill", "Item", "Beast", "Info"};
        for (const auto prefix : prefixes) {
            if (name.rfind(prefix, 0) != 0 || name.size() <= prefix.size() + 4) continue;
            const auto digits = name.substr(prefix.size(), name.size() - prefix.size() - 4);
            if (!digits.empty() && std::all_of(digits.begin(), digits.end(),
                                               [](char c) { return c >= '0' && c <= '9'; })) return true;
        }
        return false;
    };

    // DはClassicの資源を一ファイルずつ公開する。C++
    // 移植版は失敗し得る非同期/Webの保存にも対応するため
    // 、意図的に動作を拡張し、WSM/WID/WEXの保存全
    // 体が成功するまで、前の完全な管理対象一式を維持する。
    lib::Array<std::pair<String, lib::Bytes>> rollbackFiles;
    {
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
            if (error) break;
            if (!entry.is_regular_file(error)) {
                if (error) break;
                continue;
            }
            const auto name = utf8Path(entry.path().filename());
            if (name == "Summary.wsm" || isClassicResource(name))
                rollbackFiles.emplace_back(name, utils::readBinary(entry.path()));
        }
        if (error)
            throw std::filesystem::filesystem_error(
                "cannot capture Classic save rollback set", root, error);
    }
    bool classicCommitted{};
    const auto restoreClassic = [&] {
        if (classicCommitted) return;
        if (sync) {
            try { sync->sync(); } catch (...) {}
        }
        std::error_code error;
        lib::Array<std::filesystem::path> failedManagedFiles;
        for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
            if (error) break;
            if (!entry.is_regular_file(error)) {
                if (error) break;
                continue;
            }
            const auto name = utf8Path(entry.path().filename());
            if (name == "Summary.wsm" || isClassicResource(name))
                failedManagedFiles.push_back(entry.path());
        }
        if (error)
            throw std::filesystem::filesystem_error(
                "cannot clear failed Classic save", root, error);
        for (const auto& file : failedManagedFiles) {
            std::filesystem::remove(file, error);
            if (error)
                throw std::filesystem::filesystem_error(
                    "cannot clear failed Classic save", file, error);
        }
        for (const auto& [name, bytes] : rollbackFiles) {
            if (!filesync::writeFileAndSync(
                    root / fsPath(name), lib::ByteView(bytes.data(), bytes.size()),
                                            sync ? sync->storage() : nullptr))
                throw std::runtime_error(
                    "Unable to restore Classic resource after save failure: " + name);
        }
    };

    try {

    if (opt.backup && opt.backupDir.empty())
        throw std::invalid_argument("Backup directory is empty");
    const auto backupDirectory = fsPath(opt.backupDir);
    if (opt.backup && !opt.backupDir.empty() && !rollbackFiles.empty() &&
        (!std::filesystem::exists(backupDirectory) || std::filesystem::is_directory(backupDirectory))) {
        // XMLの保存経路と同様に、Dのように前のバックアップディレク
        // トリを破壊的に置換せず、完全なバックアップ世代を保持する。
        static std::atomic<std::uint64_t> sequence{};
        const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const auto backup_root = backupDirectory /
            ("cwxeditor_presave_classic_" + std::to_string(timestamp) + "_" +
             std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
        opt.runIo([&] {
            std::filesystem::create_directories(backup_root);
            for (const auto& [name, bytes] : rollbackFiles) {
                if (!filesync::writeFileAndSync(backup_root / fsPath(name),
                    lib::ByteView(bytes.data(), bytes.size()), sync ? sync->storage() : nullptr))
                    throw std::runtime_error("Unable to back up Classic resource: " + name);
            }
        });
    }

    SData d;
    d.prop = &prop;
    d.sPath = String(scenarioPath);
    d.skin = &skin;
    d.saveInnerImagePath = opt.saveInnerImagePath;
    d.logicalSort = opt.logicalSort;
    d.opt = &opt;
    d.skill = [&summary](std::uint64_t id) { return summary->skill(id); };
    d.item = [&summary](std::uint64_t id) { return summary->item(id); };
    d.beast = [&summary](std::uint64_t id) { return summary->beast(id); };

    const auto changed = summary->changedResources();
    const auto changedOnly = opt.saveChangedOnly;
    const auto shouldWrite = [&](const lib::Owner<CWXPath>& owner,
                                 const std::filesystem::path& file) {
        if (!changedOnly) return true;
        return !std::filesystem::is_regular_file(file) ||
               (owner && changed.find(owner.get()) != changed.end());
    };
    const auto writeBytes = [&](const std::filesystem::path& file,
                                const binary::ByteView bytes) {
        if (sync) {
            sync->push(file, bytes, {});
        } else if (!filesync::writeFileAndSync(file, bytes)) {
            throw std::runtime_error("Failed to write classic file: " + utf8Path(file));
        }
    };
    const auto writeText = [&](const std::filesystem::path& file, StringView value) {
        const auto* ptr = reinterpret_cast<const Byte*>(value.data());
        writeBytes(file, binary::ByteView(ptr, value.size()));
    };
    const auto writeBinary = [&](const auto& resource,
                                 const String& fileName,
        const std::function<void(ByteIO&)>& writer) {
        lib::Owner<CWXPath> owner;
        if (resource) {
            using Resource = typename std::decay_t<decltype(resource)>::element_type;
            if constexpr (std::is_same_v<Resource, Summary>) {
                auto* base = static_cast<area::AreaOwner*>(resource.get());
                owner = lib::Owner<CWXPath>(resource, static_cast<CWXPath*>(base));
            } else if constexpr (std::is_base_of_v<area::AbstractArea, Resource>) {
                auto* base = static_cast<area::AbstractArea*>(resource.get());
                owner = lib::Owner<CWXPath>(resource, static_cast<CWXPath*>(base));
            } else if constexpr (std::is_base_of_v<card::Card, Resource>) {
                auto* base = static_cast<card::Card*>(resource.get());
                owner = lib::Owner<CWXPath>(resource, static_cast<CWXPath*>(base));
            }
        }
        const auto file = root / fileName;
        if (!shouldWrite(owner, file)) {
            if (owner) putExData(d, owner);
            return;
        }
        ByteIO stream;
        writer(stream);
        const auto bytes = static_cast<const ByteIO&>(stream).bytes();
        writeBytes(file, bytes);
        if (owner) putExData(d, owner);
    };

    writeBinary(summary, "Summary.wsm", [&](ByteIO& f) { writeSummary(d, f, *summary); });
    for (const auto& value : summary->areas()) if (value) {
        writeBinary(value, "Area" + std::to_string(value->id()) + ".wid",
                    [&](ByteIO& f) { writeArea(d, f, *value); });
    }
    for (const auto& value : summary->battles()) if (value) {
        writeBinary(value, "Battle" + std::to_string(value->id()) + ".wid",
                    [&](ByteIO& f) { writeBattle(d, f, *value); });
    }
    for (const auto& value : summary->packages()) if (value) {
        writeBinary(value, "Package" + std::to_string(value->id()) + ".wid",
                    [&](ByteIO& f) { writePackage(d, f, *value); });
    }
    for (const auto& value : summary->casts()) if (value) {
        writeBinary(value, "Mate" + std::to_string(value->id()) + ".wid",
                    [&](ByteIO& f) { writeCast(d, f, *value); });
    }
    for (const auto& value : summary->skills()) if (value) {
        writeBinary(value, "Skill" + std::to_string(value->id()) + ".wid",
                    [&](ByteIO& f) { writeSkill(d, f, *value); });
    }
    for (const auto& value : summary->items()) if (value) {
        writeBinary(value, "Item" + std::to_string(value->id()) + ".wid",
                    [&](ByteIO& f) { writeItem(d, f, *value); });
    }
    for (const auto& value : summary->beasts()) if (value) {
        writeBinary(value, "Beast" + std::to_string(value->id()) + ".wid",
                    [&](ByteIO& f) { writeBeast(d, f, *value); });
    }
    for (const auto& value : summary->infos()) if (value) {
        writeBinary(value, "Info" + std::to_string(value->id()) + ".wid",
                    [&](ByteIO& f) { writeInfo(d, f, *value); });
    }

    const auto comment = saveComment(d);
    const auto imageRef = saveImageRef(d);
    const auto cardRef = saveCardRef(d);
    const auto templates = saveTemplate(*summary);
    if (!comment.empty()) writeText(root / "Comment.wex", comment);
    if (!imageRef.empty()) writeText(root / "ImageRef.wex", imageRef);
    if (!cardRef.empty()) writeText(root / "CardRef.wex", cardRef);
    if (!templates.empty()) writeText(root / "Template.wex", templates);

    // Dの変更分だけという指定は書き込みだけに適用し、不要になった管
    // 理ファイルは削除する。値で返されるカード一覧は一度だけ取得する
    // 。別々の一時値からbegin()/end()を得るとC++で
    // は未定義動作になり、Dの安定した配列スライスとは同等でない。
    const auto casts = summary->casts();
    const auto skills = summary->skills();
    const auto items = summary->items();
    const auto beasts = summary->beasts();
    const auto infos = summary->infos();
    if (sync) sync->sync();
    {
        std::error_code ec;
        lib::Array<std::filesystem::path> staleFiles;
        for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
            if (ec || !entry.is_regular_file()) continue;
            const auto name = utf8Path(entry.path().filename());
            if (isClassicResource(name)) {
                const bool nowPresent = std::filesystem::is_regular_file(entry.path()) &&
                                         (name == "Summary.wsm" ||
                                          name == "Comment.wex" || name == "ImageRef.wex" ||
                                          name == "CardRef.wex" || name == "Template.wex" ||
                                          name.rfind("Area", 0) == 0 || name.rfind("Battle", 0) == 0 ||
                                          name.rfind("Package", 0) == 0 || name.rfind("Mate", 0) == 0 ||
                                          name.rfind("Skill", 0) == 0 || name.rfind("Item", 0) == 0 ||
                                          name.rfind("Beast", 0) == 0 || name.rfind("Info", 0) == 0);
                (void)nowPresent;
                // 生成ファイルは所有する資源が存在すれば現行とする。上
                // で書いた全ファイルを保持し、古い名前だけを削除する。
                bool current = name == "Summary.wsm";
                if (!current && name == "Comment.wex") current = !comment.empty();
                if (!current && name == "ImageRef.wex") current = !imageRef.empty();
                if (!current && name == "CardRef.wex") current = !cardRef.empty();
                if (!current && name == "Template.wex") current = !templates.empty();
                if (!current) {
                    const std::array<StringView, 8> prefixes{
                        "Area", "Battle", "Package", "Mate",
                        "Skill", "Item", "Beast", "Info"};
                    for (const auto prefix : prefixes) {
                        if (name.rfind(prefix, 0) == 0) {
                            std::uint64_t id = 0;
                            if (sWith(name, prefix, id)) {
                                current = [&]() {
                                    if (prefix == "Area") return std::any_of(summary->areas().begin(), summary->areas().end(), [id](const auto& v) { return v && v->id() == id; });
                                    if (prefix == "Battle") return std::any_of(summary->battles().begin(), summary->battles().end(), [id](const auto& v) { return v && v->id() == id; });
                                    if (prefix == "Package") return std::any_of(summary->packages().begin(), summary->packages().end(), [id](const auto& v) { return v && v->id() == id; });
                                    if (prefix == "Mate") return std::any_of(casts.begin(), casts.end(), [id](const auto& v) { return v && v->id() == id; });
                                    if (prefix == "Skill") return std::any_of(skills.begin(), skills.end(), [id](const auto& v) { return v && v->id() == id; });
                                    if (prefix == "Item") return std::any_of(items.begin(), items.end(), [id](const auto& v) { return v && v->id() == id; });
                                    if (prefix == "Beast") return std::any_of(beasts.begin(), beasts.end(), [id](const auto& v) { return v && v->id() == id; });
                                    return std::any_of(infos.begin(), infos.end(), [id](const auto& v) { return v && v->id() == id; });
                                }();
                            }
                            break;
                        }
                    }
                }
                if (!current) staleFiles.push_back(entry.path());
            }
        }
        if (ec)
            throw std::filesystem::filesystem_error(
                "cannot enumerate Classic resources", root, ec);
        for (const auto& file : staleFiles) {
            std::filesystem::remove(file, ec);
            if (ec)
                throw std::filesystem::filesystem_error(
                    "cannot remove stale Classic resource", file, ec);
        }
    }
    classicCommitted = true;
    if (opt.savedCallback) opt.savedCallback();
    } catch (...) {
        const auto failure = std::current_exception();
        restoreClassic();
        std::rethrow_exception(failure);
    }
}

}  // namespace cwxeditor::cwx::cwl
