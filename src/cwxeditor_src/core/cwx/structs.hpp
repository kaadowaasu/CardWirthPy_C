#pragma once

#include "cwxeditor_src/lib/portable.hpp"
#include "cwxeditor_src/lib/array_slice.hpp"
#include "cwxeditor_src/lib/filesystem.hpp"
#include "cwxeditor_src/core/cwx/features.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/xml.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace cwxeditor::cwx::motion {
lib::String mTypeToName(types::MType type);
types::MType mTypeFromName(lib::StringView name);
}  // namespace cwxeditor::cwx::motion

namespace cwxeditor::cwx::structs {

using lib::Array;
using lib::Dictionary;
using lib::Optional;
using lib::SharedSlice;
using lib::String;
using lib::StringView;
using ::cwxeditor::cwx::features::Makings;
using ::cwxeditor::cwx::features::Nature;
using ::cwxeditor::cwx::features::Period;
using ::cwxeditor::cwx::features::Sex;
using ::cwxeditor::cwx::types::ActionCardType;
using ::cwxeditor::cwx::types::BorderingType;
using ::cwxeditor::cwx::types::BlendMode;
using ::cwxeditor::cwx::types::CType;
using ::cwxeditor::cwx::types::Element;
using ::cwxeditor::cwx::types::GradientDir;
using ::cwxeditor::cwx::types::Mental;
using ::cwxeditor::cwx::types::MType;
using ::cwxeditor::cwx::types::Physical;
using ::cwxeditor::cwx::types::Smoothing;
using ::cwxeditor::cwx::types::UpdateType;
using ::cwxeditor::cwx::xml::XNode;
namespace types = ::cwxeditor::cwx::types;

enum class WallpaperStyle : std::int32_t {
    Center = 0, Tile = 1, Expand = 2, ExpandFull = 3,
};
enum class SortDir : std::int32_t {
    Up = 1, Down = 2, None = 0,
};
enum class DialogStatus : std::int32_t {
    Top = 0, Under = 1, UnderWithCoupon = 2,
};
[[nodiscard]] DialogStatus toDialogStatus(std::int32_t dialogStatus);
enum class ShowStatusTime : std::int32_t {
    No = 0, Always = 1, WithSkin = 2,
};
[[nodiscard]] ShowStatusTime toShowStatusTime(std::int32_t showStatusTime);
enum class EditTrigger : std::int32_t {
    Quick = 0, Slow = 1,
};

struct LaunchOption {
    String conf;
    bool create = false;
    bool createclassic = false;
    String createName;
    String createSkinType;
    String createSkinName;
    String createclassicPath;
    Array<String> openPaths;
    String scenario;
    String selectfile;
    String putlangfile;
    bool noload = false;
    bool help = false;
    void parseStrings(const Array<String>& args);
};

class DSize {
public:
    virtual ~DSize() = default;
    virtual void width(std::int32_t value) = 0;
    [[nodiscard]] virtual std::int32_t width() = 0;
    virtual void height(std::int32_t value) = 0;
    [[nodiscard]] virtual std::int32_t height() = 0;
};
class WSize : public DSize {
public:
    ~WSize() override = default;
    virtual void x(std::int32_t value) = 0;
    [[nodiscard]] virtual std::int32_t x() = 0;
    virtual void y(std::int32_t value) = 0;
    [[nodiscard]] virtual std::int32_t y() = 0;
    virtual void maximized(bool value) = 0;
    [[nodiscard]] virtual bool maximized() = 0;
};

inline void requireNode(const XNode& node, StringView expected) {
    if (node.name() != expected)
        throw std::runtime_error("Node is not " + String(expected));
}

inline std::filesystem::path pathFromUtf8(StringView value) {
    if (value.empty()) return {};
    const auto* first = reinterpret_cast<const char8_t*>(value.data());
    return std::filesystem::path(std::u8string(first, value.size()));
}

inline String pathToUtf8(const std::filesystem::path& value) {
    return lib::generic_path_to_utf8(value);
}

struct CPoint {
    std::int32_t x = 0;
    std::int32_t y = 0;
    void toNode(XNode& e, String name = "point") const {
        auto r = e.newElement(name, String{});
        r.newAttr("x", x); r.newAttr("y", y);
    }
    void fromNode(XNode& node) {
        requireNode(node, "point");
        x = node.attr<std::int32_t>("x", true);
        y = node.attr<std::int32_t>("y", true);
    }
    bool operator==(const CPoint&) const = default;
};
struct CSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    void toNode(XNode& e, String name = "size") const {
        auto r = e.newElement(name, String{});
        r.newAttr("width", width); r.newAttr("height", height);
    }
    void fromNode(XNode& node) {
        width = node.attr<std::uint32_t>("width", true);
        height = node.attr<std::uint32_t>("height", true);
    }
    bool operator==(const CSize&) const = default;
};
struct CRect {
    std::int32_t x = 0, y = 0, width = 0, height = 0;
    void toNode(XNode& e, String name = "rect") const {
        auto r = e.newElement(name, String{});
        r.newAttr("x", x); r.newAttr("y", y);
        r.newAttr("width", width); r.newAttr("height", height);
    }
    void fromNode(XNode& node) {
        x = node.attr<std::int32_t>("x", true);
        y = node.attr<std::int32_t>("y", true);
        width = node.attr<std::int32_t>("width", true);
        height = node.attr<std::int32_t>("height", true);
    }
    bool operator==(const CRect&) const = default;
};
struct CInsets {
    std::int32_t n = 0, e = 0, s = 0, w = 0;
    void toNode(XNode& parent, String name = "insets") const {
        auto r = parent.newElement(name, String{});
        r.newAttr("n", n); r.newAttr("e", this->e);
        r.newAttr("s", s); r.newAttr("w", w);
    }
    void fromNode(XNode& node) {
        n = node.attr<std::int32_t>("n", true);
        e = node.attr<std::int32_t>("e", true);
        s = node.attr<std::int32_t>("s", true);
        w = node.attr<std::int32_t>("w", true);
    }
    bool operator==(const CInsets&) const = default;
};
struct CRGB {
    std::uint32_t r = 0, g = 0, b = 0, a = 255;
    void toNode(XNode& e, String name = "rgb") const {
        auto rNode = e.newElement(name, String{});
        rNode.newAttr("r", r); rNode.newAttr("g", g);
        rNode.newAttr("b", b); rNode.newAttr("a", a);
    }
    void fromNode(XNode& node) {
        r = node.attr<std::uint32_t>("r", true);
        g = node.attr<std::uint32_t>("g", true);
        b = node.attr<std::uint32_t>("b", true);
        a = node.attr<std::uint32_t>("a", false, 255);
    }
    bool operator==(const CRGB&) const = default;
};
struct CFont {
    String name;
    std::uint32_t point = 0;
    bool bold = false;
    bool italic = false;
    void toNode(XNode& e, String elementName = "font") const {
        auto r = e.newElement(elementName, String{});
        // Dの引数は同名のメンバーnameを隠すため、その動作を維持する。
        r.newAttr("name", elementName);
        r.newAttr("point", point); r.newAttr("bold", bold);
        r.newAttr("italic", italic);
    }
    void fromNode(XNode& node) {
        name = node.attr<String>("name", true);
        point = node.attr<std::uint32_t>("point", true);
        bold = node.attr<bool>("bold", true);
        italic = node.attr<bool>("italic", true);
    }
    bool operator==(const CFont&) const = default;
};

struct BgImageSetting {
    inline static constexpr StringView XML_NAME = "bgImageSetting";
    String name;
    std::int32_t x = 0, y = 0, width = 0, height = 0;
    bool mask = false;
    std::int32_t layer = 0;
    [[nodiscard]] BgImageSetting dup() const { return *this; }
    static BgImageSetting opCall(String name, std::int32_t x, std::int32_t y,
                                 std::int32_t width, std::int32_t height,
                                 bool mask, std::int32_t layer) {
        BgImageSetting result;
        result.name = std::move(name); result.x = x; result.y = y;
        result.width = width; result.height = height;
        result.mask = mask; result.layer = layer;
        return result;
    }
    [[nodiscard]] XNode toNode() const {
        auto e = XNode::create(XML_NAME, String{}); toNodeImpl(e); return e;
    }
    void toNode(XNode& node) const {
        auto e = node.newElement(XML_NAME, String{}); toNodeImpl(e);
    }
    void fromNode(XNode& node) {
        name = node.childText<String>("name", true);
        x = node.childText<std::int32_t>("x", true);
        y = node.childText<std::int32_t>("y", true);
        width = node.childText<std::int32_t>("width", true);
        height = node.childText<std::int32_t>("height", true);
        mask = node.childText<bool>("mask", true);
        layer = node.childText<std::int32_t>("layer", false, 0);
    }
    bool operator==(const BgImageSetting&) const = default;
private:
    void toNodeImpl(XNode& e) const {
        e.newElement("name", name); e.newElement("x", x);
        e.newElement("y", y); e.newElement("width", width);
        e.newElement("height", height); e.newElement("mask", mask);
        e.newElement("layer", layer);
    }
};

[[nodiscard]] String parseDollarParams(StringView format,
                                        const Dictionary<char, String>& params);

struct OuterTool {
    inline static constexpr StringView XML_NAME = "tool";
    String name, command, workDir, mnemonic, hotkey;
    [[nodiscard]] OuterTool dup() const { return *this; }
    static OuterTool opCall(String name, String command, String workDir,
                            String mnemonic, String hotkey) {
        OuterTool result;
        result.name = std::move(name); result.command = std::move(command);
        result.workDir = std::move(workDir); result.mnemonic = std::move(mnemonic);
        result.hotkey = std::move(hotkey); return result;
    }
    [[nodiscard]] XNode toNode() const {
        auto e = XNode::create(XML_NAME, String{}); toNodeImpl(e); return e;
    }
    void toNode(XNode& node) const {
        auto e = node.newElement(XML_NAME, String{}); toNodeImpl(e);
    }
    void fromNode(XNode& node) {
        name = node.childText<String>("name", true);
        command = node.childText<String>("command", true);
        workDir = node.childText<String>("workDir", true);
        mnemonic = node.attr<String>("mnemonic", false, "");
        hotkey = node.attr<String>("hotkey", false, "");
    }
    static String parse(String format, String file, String sPath) {
        Dictionary<char, String> params;
        params.emplace('F', std::move(file)); params.emplace('S', std::move(sPath));
        return parseDollarParams(format, params);
    }
    bool operator==(const OuterTool&) const = default;
private:
    void toNodeImpl(XNode& e) const {
        e.newElement("name", name); e.newElement("command", command);
        e.newElement("workDir", workDir);
        if (!mnemonic.empty()) e.newAttr("mnemonic", mnemonic);
        if (!hotkey.empty()) e.newAttr("hotkey", hotkey);
    }
};

struct BgImageS {
    inline static constexpr StringView XML_NAME = "background";
    String type;
    std::int32_t x = 0, y = 0;
    std::uint32_t width = 0, height = 0;
    bool mask = false;
    std::int32_t layer = 0;
    String cellName;
    Smoothing smoothing = Smoothing::Default;
    String name, text, fontName;
    std::uint32_t size = 0;
    CRGB color;
    bool bold = false, italic = false, underline = false, strike = false;
    bool vertical = false, antialias = false;
    BorderingType borderingType = BorderingType::None;
    CRGB borderingColor;
    std::uint32_t borderingWidth = 0;
    UpdateType updateType = UpdateType::Fixed;
    BlendMode blendMode = BlendMode::Normal;
    GradientDir gradientDir = GradientDir::None;
    CRGB color1, color2;
    std::uint32_t pcNumber = 0;
    bool expand = false;

    BgImageS() = default;
    BgImageS(String imageName, const std::int32_t imageX,
             const std::int32_t imageY, const std::int32_t imageWidth,
             const std::int32_t imageHeight, const bool imageMask)
        : type("image"), x(imageX), y(imageY),
          width(static_cast<std::uint32_t>(imageWidth)),
          height(static_cast<std::uint32_t>(imageHeight)), mask(imageMask),
          name(std::move(imageName)) {}

    static BgImageS opCall(String name, std::int32_t x, std::int32_t y,
                           std::int32_t width, std::int32_t height, bool mask) {
        return BgImageS(std::move(name), x, y, width, height, mask);
    }
    void toNode(XNode& e) const {
        auto r = e.newElement(XML_NAME, String{});
        r.newAttr("type", type); r.newAttr("x", x); r.newAttr("y", y);
        r.newAttr("width", width); r.newAttr("height", height);
        r.newAttr("mask", mask); r.newAttr("cellName", cellName);
        r.newAttr("layer", std::to_string(layer));
        if (type == "image") {
            r.newAttr("name", name);
            r.newAttr("smoothing", types::fromSmoothing(smoothing));
        } else if (type == "text") {
            r.newElement("text", text);
            auto f = r.newElement("font", fontName);
            f.newAttr("size", size);
            if (bold) f.newAttr("bold", bold);
            if (italic) f.newAttr("italic", italic);
            if (underline) f.newAttr("underline", underline);
            if (strike) f.newAttr("strike", strike);
            if (vertical) f.newAttr("vertical", vertical);
            if (antialias) f.newAttr("antialias", antialias);
            color.toNode(r);
            if (borderingType != BorderingType::None) {
                auto b = r.newElement("bordering", String{});
                b.newAttr("type", types::fromBorderingType(borderingType));
                b.newAttr("width", borderingWidth); borderingColor.toNode(b);
            }
            r.newAttr("updateType", types::fromUpdateType(updateType));
        } else if (type == "color") {
            r.newAttr("blendMode", types::fromBlendMode(blendMode));
            color1.toNode(r);
            if (gradientDir != GradientDir::None) {
                auto g = r.newElement("gradient", String{});
                g.newAttr("direction", types::fromGradientDir(gradientDir));
                color2.toNode(g);
            }
        } else if (type == "pc") {
            r.newAttr("pcNumber", std::to_string(pcNumber));
            r.newAttr("expand", expand);
            r.newAttr("smoothing", types::fromSmoothing(smoothing));
        } else {
            throw std::runtime_error("Unknown type: " + type);
        }
    }
    void fromNode(XNode& node) {
        requireNode(node, XML_NAME);
        type = node.attr<String>("type", false, "image");
        x = node.attr<std::int32_t>("x", true);
        y = node.attr<std::int32_t>("y", true);
        width = node.attr<std::uint32_t>("width", true);
        height = node.attr<std::uint32_t>("height", true);
        mask = node.attr<bool>("mask", false, false);
        layer = node.attr<std::int32_t>("layer", false, 0);
        cellName = node.attr<String>("cellName", false, "");
        if (type == "image") {
            name = node.attr<String>("name", true);
            smoothing = types::toSmoothing(node.attr<String>("smoothing", false, "Default"));
        } else if (type == "text") {
            node.onTag["text"] = [this](XNode& child) { text = child.value(); };
            node.onTag["font"] = [this](XNode& child) {
                fontName = child.value();
                size = child.attr<std::uint32_t>("size", true);
                bold = child.attr<bool>("bold", false, bold);
                italic = child.attr<bool>("italic", false, italic);
                underline = child.attr<bool>("underline", false, underline);
                strike = child.attr<bool>("strike", false, strike);
                vertical = child.attr<bool>("vertical", false, vertical);
                antialias = child.attr<bool>("antialias", false, antialias);
            };
            node.onTag["rgb"] = [this](XNode& child) { color.fromNode(child); };
            node.onTag["bordering"] = [this](XNode& child) {
                borderingType = types::toBorderingType(child.attr<String>("type", true));
                borderingWidth = child.attr<std::uint32_t>("width", true);
                child.onTag["rgb"] = [this](XNode& rgb) { borderingColor.fromNode(rgb); };
                child.parse();
            };
            updateType = types::toUpdateType(node.attr<String>("updateType", false, "Variables"));
            node.parse();
        } else if (type == "color") {
            bool hasColor2 = false;
            blendMode = types::toBlendMode(node.attr<String>("blendMode", true));
            node.onTag["rgb"] = [this](XNode& child) { color1.fromNode(child); };
            node.onTag["gradient"] = [this, &hasColor2](XNode& child) {
                gradientDir = types::toGradientDir(child.attr<String>("direction", true));
                child.onTag["rgb"] = [this, &hasColor2](XNode& rgb) {
                    color2.fromNode(rgb); hasColor2 = true;
                };
                child.parse();
            };
            node.parse();
            if (!hasColor2) color2 = color1;
        } else if (type == "pc") {
            pcNumber = node.attr<std::uint32_t>("pcNumber", true);
            expand = node.attr<bool>("expand", false, false);
            smoothing = types::toSmoothing(node.attr<String>("smoothing", false, "Default"));
            node.parse();
        } else {
            throw std::runtime_error("Unknown type: " + type);
        }
    }
    bool operator==(const BgImageS&) const = default;
};

[[nodiscard]] String fromSex(Sex e);
[[nodiscard]] Sex toSex(StringView e);
[[nodiscard]] String fromPeriod(Period e);
[[nodiscard]] Period toPeriod(StringView e);
[[nodiscard]] String fromNature(Nature e);
[[nodiscard]] Nature toNature(StringView e);
[[nodiscard]] String fromMakings(Makings e);
[[nodiscard]] Makings toMakings(StringView e);

struct ClassicEngine {
    inline static constexpr StringView XML_NAME = "classicEngine";
    String name, enginePath, dataDirName, execute, mnemonic, hotkey, type;
    Optional<String> okText;
    Dictionary<String, String> sexName, periodName, natureName, makingsName;
    Dictionary<ActionCardType, String> actionCardName;
    Dictionary<Sex, Dictionary<Physical, std::int32_t>> physicalModSex;
    Dictionary<Period, Dictionary<Physical, std::int32_t>> physicalModPeriod;
    Dictionary<Nature, Dictionary<Physical, std::int32_t>> physicalModNature;
    Dictionary<Makings, Dictionary<Physical, std::int32_t>> physicalModMakings;
    Dictionary<Sex, Dictionary<Mental, double>> mentalModSex;
    Dictionary<Period, Dictionary<Mental, double>> mentalModPeriod;
    Dictionary<Nature, Dictionary<Mental, double>> mentalModNature;
    Dictionary<Makings, Dictionary<Mental, double>> mentalModMakings;

    [[nodiscard]] ClassicEngine dup() const { return *this; }
    [[nodiscard]] String executePath(StringView appPath, bool engine) const;
    [[nodiscard]] XNode toNode() const {
        auto e = XNode::create(XML_NAME, String{}); toNodeImpl(e); return e;
    }
    void toNode(XNode& node) const {
        auto e = node.newElement(XML_NAME, String{}); toNodeImpl(e);
    }
    [[nodiscard]] String legacyName() const;
    void clearFeatures() {
        okText.reset(); sexName.clear(); periodName.clear();
        natureName.clear(); makingsName.clear(); actionCardName.clear();
        physicalModSex.clear(); physicalModPeriod.clear();
        physicalModNature.clear(); physicalModMakings.clear();
        mentalModSex.clear(); mentalModPeriod.clear();
        mentalModNature.clear(); mentalModMakings.clear();
    }
    void fromNode(XNode& node) {
        requireNode(node, XML_NAME); clearFeatures();
        name = node.attr<String>("name", true);
        enginePath = node.attr<String>("enginePath", true);
        dataDirName = node.attr<String>("dataDirName", true);
        execute = node.attr<String>("execute", true);
        mnemonic = node.attr<String>("mnemonic", false, "");
        hotkey = node.attr<String>("hotkey", false, "");
        okText = node.hasAttr("okText")
            ? Optional<String>(node.attr<String>("okText", false, ""))
            : std::nullopt;
        type = node.attr<String>("type", false, "");
        readNameMap(node, "sexName", sexName);
        readNameMap(node, "periodName", periodName);
        readNameMap(node, "natureName", natureName);
        readNameMap(node, "makingsName", makingsName);
        node.onTag["actionCardName"] = [this](XNode& group) {
            group.onTag["name"] = [this](XNode& item) {
                const auto key = item.attr<std::int32_t>("key", true);
                actionCardName[static_cast<ActionCardType>(key)] = item.value();
            };
            group.parse();
        };
        getAA(node, "sexPhysical", physicalModSex,
              [](StringView value) { return toSex(value); },
              [](StringView value) { return types::toPhysical(String(value)); });
        getAA(node, "periodPhysical", physicalModPeriod,
              [](StringView value) { return toPeriod(value); },
              [](StringView value) { return types::toPhysical(String(value)); });
        getAA(node, "naturePhysical", physicalModNature,
              [](StringView value) { return toNature(value); },
              [](StringView value) { return types::toPhysical(String(value)); });
        getAA(node, "makingsPhysical", physicalModMakings,
              [](StringView value) { return toMakings(value); },
              [](StringView value) { return types::toPhysical(String(value)); });
        getAA(node, "sexMental", mentalModSex,
              [](StringView value) { return toSex(value); },
              [](StringView value) { return types::toMental(String(value)); });
        getAA(node, "periodMental", mentalModPeriod,
              [](StringView value) { return toPeriod(value); },
              [](StringView value) { return types::toMental(String(value)); });
        getAA(node, "natureMental", mentalModNature,
              [](StringView value) { return toNature(value); },
              [](StringView value) { return types::toMental(String(value)); });
        getAA(node, "makingsMental", mentalModMakings,
              [](StringView value) { return toMakings(value); },
              [](StringView value) { return types::toMental(String(value)); });
        node.parse();
    }
private:
    void toNodeImpl(XNode& e) const {
        e.newAttr("name", name); e.newAttr("enginePath", enginePath);
        e.newAttr("dataDirName", dataDirName); e.newAttr("execute", execute);
        if (!type.empty()) e.newAttr("type", type);
        if (!mnemonic.empty()) e.newAttr("mnemonic", mnemonic);
        if (!hotkey.empty()) e.newAttr("hotkey", hotkey);
        if (okText.has_value()) e.newAttr("okText", *okText);
        writeNameMap(e, "sexName", sexName); writeNameMap(e, "periodName", periodName);
        writeNameMap(e, "natureName", natureName); writeNameMap(e, "makingsName", makingsName);
        if (!actionCardName.empty()) {
            auto group = e.newElement("actionCardName", String{});
            for (const auto& [key, value] : actionCardName) {
                auto item = group.newElement("name", value);
                item.newAttr("key", static_cast<std::int32_t>(key));
            }
        }
        putAA(e, "sexPhysical", physicalModSex,
              [](Sex key) { return fromSex(key); },
              [](Physical key) { return types::fromPhysical(key); });
        putAA(e, "periodPhysical", physicalModPeriod,
              [](Period key) { return fromPeriod(key); },
              [](Physical key) { return types::fromPhysical(key); });
        putAA(e, "naturePhysical", physicalModNature,
              [](Nature key) { return fromNature(key); },
              [](Physical key) { return types::fromPhysical(key); });
        putAA(e, "makingsPhysical", physicalModMakings,
              [](Makings key) { return fromMakings(key); },
              [](Physical key) { return types::fromPhysical(key); });
        putAA(e, "sexMental", mentalModSex,
              [](Sex key) { return fromSex(key); },
              [](Mental key) { return types::fromMental(key); });
        putAA(e, "periodMental", mentalModPeriod,
              [](Period key) { return fromPeriod(key); },
              [](Mental key) { return types::fromMental(key); });
        putAA(e, "natureMental", mentalModNature,
              [](Nature key) { return fromNature(key); },
              [](Mental key) { return types::fromMental(key); });
        putAA(e, "makingsMental", mentalModMakings,
              [](Makings key) { return fromMakings(key); },
              [](Mental key) { return types::fromMental(key); });
    }
    static void writeNameMap(XNode& e, StringView element,
                             const Dictionary<String, String>& values) {
        if (values.empty()) return;
        auto group = e.newElement(element, String{});
        for (const auto& [key, value] : values) {
            auto item = group.newElement("name", value); item.newAttr("key", key);
        }
    }
    static void readNameMap(XNode& node, StringView element,
                            Dictionary<String, String>& values) {
        node.onTag[String(element)] = [&values](XNode& group) {
            group.onTag["name"] = [&values](XNode& item) {
                if (item.hasAttr("key")) {
                    values[item.attr<String>("key", true)] = item.value();
                }
            };
            group.parse();
        };
    }
    template <typename P, typename E, typename R, typename EName, typename PName>
    static void putAA(XNode& e, StringView element,
                      const Dictionary<E, Dictionary<P, R>>& values,
                      EName eName, PName pName) {
        if (values.empty()) return;
        auto group = e.newElement(element, String{});
        for (const auto& [outerKey, inner] : values) {
            if (inner.empty()) continue;
            auto params = group.newElement("params", String{});
            params.newAttr("key", eName(outerKey));
            for (const auto& [innerKey, value] : inner) {
                auto item = params.newElement("value", value);
                item.newAttr("key", pName(innerKey));
            }
        }
    }
    template <typename P, typename E, typename R, typename EFrom, typename PFrom>
    static void getAA(XNode& node, StringView element,
                      Dictionary<E, Dictionary<P, R>>& values,
                      EFrom eFrom, PFrom pFrom) {
        node.onTag[String(element)] = [&values, eFrom, pFrom](XNode& group) {
            Dictionary<P, R> inner;
            const auto outerName = group.attr<String>("key", false, "");
            if (!outerName.empty()) {
                const E outerKey = eFrom(outerName);
                group.onTag["params"] = [&inner, pFrom](XNode& params) {
                    const auto innerName = params.attr<String>("key", false, "");
                    if (!innerName.empty())
                        inner[pFrom(innerName)] = params.template valueTo<R>();
                };
                group.parse();
                if (!inner.empty()) values[outerKey] = std::move(inner);
            }
        };
    }
};

struct ScTemplate {
    inline static constexpr StringView XML_NAME = "scenarioTemplate";
    String name, path;
    [[nodiscard]] XNode toNode() const {
        auto e = XNode::create(XML_NAME, String{}); toNodeImpl(e); return e;
    }
    void toNode(XNode& node) const {
        auto e = node.newElement(XML_NAME, String{}); toNodeImpl(e);
    }
    void fromNode(XNode& node) {
        requireNode(node, XML_NAME);
        name = node.attr<String>("name", true); path = node.attr<String>("path", true);
    }
private:
    void toNodeImpl(XNode& e) const {
        e.newAttr("name", name); e.newAttr("path", path);
    }
};
struct EvTemplate {
    inline static constexpr StringView XML_NAME = "eventTemplate";
    String name, script, mnemonic, hotkey;
    [[nodiscard]] XNode toNode() const {
        auto e = XNode::create(XML_NAME, script); toNodeImpl(e); return e;
    }
    void toNode(XNode& node) const {
        auto e = node.newElement(XML_NAME, script); toNodeImpl(e);
    }
    void fromNode(XNode& node) {
        requireNode(node, XML_NAME);
        name = node.attr<String>("name", true);
        mnemonic = node.attr<String>("mnemonic", false, "");
        hotkey = node.attr<String>("hotkey", false, "");
        script = node.value();
    }
private:
    void toNodeImpl(XNode& e) const {
        e.newAttr("name", name);
        if (!mnemonic.empty()) e.newAttr("mnemonic", mnemonic);
        if (!hotkey.empty()) e.newAttr("hotkey", hotkey);
    }
};
struct OpenHistory {
    inline static constexpr StringView XML_NAME = "openHistory";
    String path, skinType, skinName, skinEngine;
    [[nodiscard]] XNode toNode() const {
        auto e = XNode::create(XML_NAME, path); toNodeImpl(e); return e;
    }
    [[nodiscard]] XNode toNode(XNode& node) const {
        auto e = node.newElement(XML_NAME, path); toNodeImpl(e); return e;
    }
    void fromNode(XNode& node) {
        skinType = node.attr<String>("skinType", false, "");
        skinName = node.attr<String>("skinName", false, "");
        skinEngine = node.attr<String>("skinEngine", false, "");
        path = node.value();
    }
private:
    void toNodeImpl(XNode& e) const {
        if (!skinType.empty()) e.newAttr("skinType", skinType);
        if (!skinName.empty()) e.newAttr("skinName", skinName);
        if (!skinEngine.empty()) e.newAttr("skinEngine", skinEngine);
    }
};
struct ExecutionParty {
    inline static constexpr StringView XML_NAME = "executionParty";
    String engineName, enginePath;
    bool isClassic = false;
    String yadoName, yadoPath, partyName, partyPath;
    [[nodiscard]] XNode toNode() const {
        auto e = XNode::create(XML_NAME, String{}); toNodeImpl(e); return e;
    }
    [[nodiscard]] XNode toNode(XNode& node, String name = String(XML_NAME)) const {
        auto e = node.newElement(name, String{}); toNodeImpl(e); return e;
    }
    void fromNode(XNode& node) {
        engineName = node.attr<String>("engineName", false, "");
        enginePath = node.attr<String>("enginePath", false, "");
        isClassic = node.attr<bool>("classic", false, false);
        yadoName = node.attr<String>("yadoName", false, "");
        yadoPath = node.attr<String>("yadoPath", false, "");
        partyName = node.attr<String>("partyName", false, "");
        partyPath = node.attr<String>("partyPath", false, "");
    }
private:
    void toNodeImpl(XNode& e) const {
        if (enginePath.empty()) return;
        e.newAttr("engineName", engineName); e.newAttr("enginePath", enginePath);
        e.newAttr("classic", isClassic); e.newAttr("yadoName", yadoName);
        e.newAttr("yadoPath", yadoPath); e.newAttr("partyName", partyName);
        e.newAttr("partyPath", partyPath);
    }
};

struct ContentConversionGroup {
    inline static constexpr StringView XML_NAME = "contentConversionGroup";
    Array<CType> group;
    [[nodiscard]] XNode toNode() const;
    void toNode(XNode& node, String name = String(XML_NAME)) const;
    void fromNode(XNode& node);
private:
    void toNodeImpl(XNode& node) const;
};
struct Tool {
    inline static constexpr StringView XML_NAME = "toolItem";
    bool separator = false;
    types::MenuID menu = types::MenuID::None;
    Tool() = default;
    explicit Tool(types::MenuID id) : separator(false), menu(id) {}
    bool operator==(const Tool&) const = default;
    static Tool opCall(types::MenuID id) { return Tool(id); }
    static Tool opCall() {
        Tool result;
        result.separator = true;
        return result;
    }
    [[nodiscard]] XNode toNode() const;
    void toNode(XNode& node) const;
    void fromNode(XNode& node);
private:
    void toNodeImpl(XNode& e) const {
        if (separator) e.newAttr("separator", true);
    }
};
struct ToolBarSettings {
    Array<Array<Tool>> tools;
    ToolBarSettings() = default;
    explicit ToolBarSettings(Array<Array<Tool>> initialTools)
        : tools(std::move(initialTools)) {}
    bool operator==(const ToolBarSettings&) const = default;
    static ToolBarSettings opCall(Array<Array<Tool>> tools);
    [[nodiscard]] ToolBarSettings dup() const;
    void toNode(XNode& e, String name = "toolBarSettings") const;
    void fromNode(XNode& node);
};
struct SoundFontWithVolume {
    String path;
    std::uint32_t volume = 0;
    SoundFontWithVolume() = default;
    SoundFontWithVolume(String path, std::uint32_t volume)
        : path(std::move(path)), volume(volume) {}
    bool operator==(const SoundFontWithVolume&) const = default;
};
struct KeyCodeByFeature {
    inline static constexpr StringView XML_NAME = "keyCodeByFeature";
    String feature, keyCode;
    void toNode(XNode& e, String name = String(XML_NAME)) const {
        auto r = e.newElement(name, keyCode); r.newAttr("feature", feature);
    }
    void fromNode(XNode& node) {
        feature = node.attr<String>("feature", true); keyCode = node.value();
    }
    bool operator==(const KeyCodeByFeature&) const = default;
};
struct KeyCodeByMotion {
    inline static constexpr StringView XML_NAME = "keyCodeByMotion";
    MType type = static_cast<MType>(0);
    bool hasElement = false;
    Element element = Element::All;
    String keyCode;
    [[nodiscard]] bool match(MType candidateType, Element candidateElement) const {
        return type == candidateType && (!hasElement || element == candidateElement);
    }
    void toNode(XNode& e, String name = String(XML_NAME)) const {
        auto r = e.newElement(name, keyCode);
        r.newAttr("type", motion::mTypeToName(type));
        if (hasElement) r.newAttr("element", types::fromElement(element));
    }
    void fromNode(XNode& node) {
        type = motion::mTypeFromName(node.attr<String>("type", true));
        const auto elementName = node.attr<String>("element", false, "");
        hasElement = !elementName.empty();
        element = hasElement ? types::toElement(elementName) : Element::All;
        keyCode = node.value();
    }
    [[nodiscard]] bool operator==(const KeyCodeByMotion& other) const {
        return type == other.type && hasElement == other.hasElement &&
               (!hasElement || element == other.element) && keyCode == other.keyCode;
    }
    [[nodiscard]] std::size_t toHash() const noexcept {
        std::size_t hash = static_cast<std::size_t>(type);
        hash = hash * 9 + (hasElement ? 1u : 0u);
        if (hasElement) hash = hash * 9 + static_cast<std::size_t>(element);
        for (const unsigned char c : keyCode) hash = hash * 9 + c;
        return hash;
    }
};
struct ElementOverride {
    inline static constexpr StringView XML_NAME = "elementOverride";
    Element element = Element::All;
    String icon, name, targetType;
    void toNode(XNode& e, String nodeName = String(XML_NAME)) const {
        auto r = e.newElement(nodeName, String{});
        r.newAttr("element", types::fromElement(element));
        if (!icon.empty()) r.newAttr("icon", icon);
        if (!name.empty()) r.newAttr("name", name);
        if (!targetType.empty()) r.newAttr("targetType", targetType);
    }
    void fromNode(XNode& node) {
        element = types::toElement(node.attr<String>("element", true));
        icon = node.attr<String>("icon", false, "");
        name = node.attr<String>("name", false, "");
        targetType = node.attr<String>("targetType", false, "");
    }
    bool operator==(const ElementOverride&) const = default;
};

struct SettingsWithSkinType {
    inline static constexpr StringView XML_NAME = "settingsWithSkinType";
    String type;
    bool overrideBgImages = false, overrideSelections = false, overrideKeyCodes = false;
    SharedSlice<BgImageS> bgImagesDefault;
    SharedSlice<BgImageSetting> bgImageSettings;
    SharedSlice<String> standardSelections, standardKeyCodes;
    SharedSlice<KeyCodeByFeature> keyCodesByFeatures;
    SharedSlice<KeyCodeByMotion> keyCodesByMotions;
    SharedSlice<ElementOverride> elementOverrides;
    void toNode(XNode& e, const SettingsWithSkinType& initValue,
                String name = String(XML_NAME)) const {
        auto r = e.newElement(name, String{}); r.newAttr("type", type);
        if (initValue.type.empty() || initValue.overrideBgImages != overrideBgImages)
            r.newAttr("bgImage", overrideBgImages ? "override" : "default");
        if (initValue.type.empty() || initValue.overrideSelections != overrideSelections)
            r.newAttr("selection", overrideSelections ? "override" : "default");
        if (initValue.type.empty() || initValue.overrideKeyCodes != overrideKeyCodes)
            r.newAttr("keyCode", overrideKeyCodes ? "override" : "default");
        if (initValue.type.empty() || initValue.bgImagesDefault != bgImagesDefault) {
            auto c = r.newElement("bgImagesDefault", String{});
            for (const auto& item : bgImagesDefault) item.toNode(c);
        }
        if (initValue.type.empty() || initValue.bgImageSettings != bgImageSettings) {
            auto c = r.newElement("bgImageSettings", String{});
            for (const auto& item : bgImageSettings) item.toNode(c);
        }
        if (initValue.type.empty() || initValue.standardSelections != standardSelections) {
            auto c = r.newElement("standardSelections", String{});
            for (const auto& item : standardSelections) c.newElement("value", item);
        }
        if (initValue.type.empty() || initValue.standardKeyCodes != standardKeyCodes) {
            auto c = r.newElement("standardKeyCodes", String{});
            for (const auto& item : standardKeyCodes) c.newElement("value", item);
        }
        if (initValue.type.empty() || initValue.keyCodesByFeatures != keyCodesByFeatures) {
            auto c = r.newElement("keyCodesByFeatures", String{});
            for (const auto& item : keyCodesByFeatures) item.toNode(c);
        }
        if (initValue.type.empty() || initValue.keyCodesByMotions != keyCodesByMotions) {
            auto c = r.newElement("keyCodesByMotions", String{});
            for (const auto& item : keyCodesByMotions) item.toNode(c);
        }
        if (initValue.type.empty() || initValue.elementOverrides != elementOverrides) {
            auto c = r.newElement("elementOverrides", String{});
            for (const auto& item : elementOverrides) item.toNode(c);
        }
    }
    void fromNode(XNode& node, const SettingsWithSkinType& initValue) {
        type = node.attr<String>("type", true);
        overrideBgImages = node.attr<String>(
            "bgImage", false, initValue.overrideBgImages ? "override" : "default") == "override";
        overrideSelections = node.attr<String>(
            "selection", false, initValue.overrideSelections ? "override" : "default") == "override";
        overrideKeyCodes = node.attr<String>(
            "keyCode", false, initValue.overrideKeyCodes ? "override" : "default") == "override";
        bgImagesDefault = lib::dupSharedSlice(initValue.bgImagesDefault);
        bgImageSettings = lib::dupSharedSlice(initValue.bgImageSettings);
        standardSelections = lib::dupSharedSlice(initValue.standardSelections);
        standardKeyCodes = lib::dupSharedSlice(initValue.standardKeyCodes);
        keyCodesByFeatures = lib::dupSharedSlice(initValue.keyCodesByFeatures);
        keyCodesByMotions = lib::dupSharedSlice(initValue.keyCodesByMotions);
        elementOverrides = lib::dupSharedSlice(initValue.elementOverrides);
        node.onTag["bgImagesDefault"] = [this](XNode& c) {
            bgImagesDefault = {};
            c.onTag[String(BgImageS::XML_NAME)] = [this](XNode& child) {
                lib::appendToSlice(bgImagesDefault, BgImageS{});
                bgImagesDefault.back().fromNode(child);
            };
            c.parse();
        };
        node.onTag["bgImageSettings"] = [this](XNode& c) {
            bgImageSettings = {};
            c.onTag[String(BgImageSetting::XML_NAME)] = [this](XNode& child) {
                lib::appendToSlice(bgImageSettings, BgImageSetting{});
                bgImageSettings.back().fromNode(child);
            };
            c.parse();
        };
        node.onTag["standardSelections"] = [this](XNode& c) {
            standardSelections = {};
            c.onTag["value"] = [this](XNode& child) {
                lib::appendToSlice(standardSelections, child.value());
            };
            c.parse();
        };
        node.onTag["standardKeyCodes"] = [this](XNode& c) {
            standardKeyCodes = {};
            c.onTag["value"] = [this](XNode& child) {
                lib::appendToSlice(standardKeyCodes, child.value());
            };
            c.parse();
        };
        node.onTag["keyCodesByFeatures"] = [this](XNode& c) {
            keyCodesByFeatures = {};
            c.onTag[String(KeyCodeByFeature::XML_NAME)] = [this](XNode& child) {
                lib::appendToSlice(keyCodesByFeatures, KeyCodeByFeature{});
                keyCodesByFeatures.back().fromNode(child);
            };
            c.parse();
        };
        node.onTag["keyCodesByMotions"] = [this](XNode& c) {
            keyCodesByMotions = {};
            c.onTag[String(KeyCodeByMotion::XML_NAME)] = [this](XNode& child) {
                lib::appendToSlice(keyCodesByMotions, KeyCodeByMotion{});
                keyCodesByMotions.back().fromNode(child);
            };
            c.parse();
        };
        node.onTag["elementOverrides"] = [this](XNode& c) {
            elementOverrides = {};
            c.onTag[String(ElementOverride::XML_NAME)] = [this](XNode& child) {
                lib::appendToSlice(elementOverrides, ElementOverride{});
                elementOverrides.back().fromNode(child);
            };
            c.parse();
        };
        node.parse();
    }
    bool operator==(const SettingsWithSkinType&) const = default;
};

struct SettingsWithSkinTypeList {
    inline static constexpr StringView XML_NAME = "settingsWithSkinTypes";
    SharedSlice<SettingsWithSkinType> values;
    explicit SettingsWithSkinTypeList(SharedSlice<SettingsWithSkinType> values)
        : SettingsWithSkinTypeList(std::move(values), true) {}
    operator SharedSlice<SettingsWithSkinType>&() noexcept { return values; }
    operator SharedSlice<const SettingsWithSkinType>() const noexcept { return values; }
    void toNode(XNode& e, String name = String(XML_NAME)) const {
        Dictionary<String, const SettingsWithSkinType*> table;
        for (const auto& value : initValues) {
            assert(table.find(value.type) == table.end()); table[value.type] = &value;
        }
        auto node = e.newElement(name, String{});
        for (const auto& value : values) {
            const auto found = table.find(value.type);
            if (found != table.end()) value.toNode(node, *found->second);
            else value.toNode(node, SettingsWithSkinType{});
        }
    }
    void fromNode(XNode& node) {
        Dictionary<String, const SettingsWithSkinType*> table;
        for (const auto& value : initValues) {
            assert(table.find(value.type) == table.end()); table[value.type] = &value;
        }
        values = {};
        node.onTag[String(SettingsWithSkinType::XML_NAME)] = [this, &table](XNode& child) {
            lib::appendToSlice(values, SettingsWithSkinType{});
            const auto typeName = child.attr<String>("type", true);
            const auto found = table.find(typeName);
            if (found != table.end()) values.back().fromNode(child, *found->second);
            else values.back().fromNode(child, SettingsWithSkinType{});
        };
        node.parse();
    }
private:
    // 通常は到達しない、基準外のコンストラクターへのテスト専用アクセ
    // ス。 friend宣言は実行時の動作や公開APIを追加しない。
    friend struct SettingsWithSkinTypeListTestAccess;
    SettingsWithSkinTypeList(SharedSlice<SettingsWithSkinType> initial, bool init)
        : values(std::move(initial)),
          initValues(init
                         ? SharedSlice<const SettingsWithSkinType>{
                               lib::dupSharedSlice(values)}
                         : SharedSlice<const SettingsWithSkinType>{}) {}
    SharedSlice<const SettingsWithSkinType> initValues;
};

struct SkinTypeByClassicEngine {
    inline static constexpr StringView XML_NAME = "skinTypeByClassicEngine";
    String type, engine, directory;
    void toNode(XNode& e, String name = String(XML_NAME)) const {
        auto r = e.newElement(name, type);
        if (!engine.empty()) r.newAttr("engine", engine);
        if (!directory.empty()) r.newAttr("directory", directory);
    }
    void fromNode(XNode& node) {
        type = node.value();
        engine = node.attr<String>("engine", false, "");
        directory = node.attr<String>("directory", false, "");
    }
    bool operator==(const SkinTypeByClassicEngine&) const = default;
};

}  // namespace cwxeditor::cwx::structs
