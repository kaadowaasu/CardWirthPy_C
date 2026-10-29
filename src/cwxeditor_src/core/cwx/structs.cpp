#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/menu.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <new>
#include <stdexcept>

#include "cwxeditor_src/core/cwx/event.hpp"

namespace cwxeditor::cwx::structs {

DialogStatus toDialogStatus(std::int32_t dialogStatus) {
    switch (dialogStatus) {
    case static_cast<std::int32_t>(DialogStatus::Top):
    case static_cast<std::int32_t>(DialogStatus::Under):
    case static_cast<std::int32_t>(DialogStatus::UnderWithCoupon):
        return static_cast<DialogStatus>(dialogStatus);
    default:
        return DialogStatus::Top;
    }
}

ShowStatusTime toShowStatusTime(std::int32_t showStatusTime) {
    switch (showStatusTime) {
    case static_cast<std::int32_t>(ShowStatusTime::No):
    case static_cast<std::int32_t>(ShowStatusTime::Always):
    case static_cast<std::int32_t>(ShowStatusTime::WithSkin):
        return static_cast<ShowStatusTime>(showStatusTime);
    default:
        return ShowStatusTime::Always;
    }
}

// EditorのDのstd.pathの動作に合わせ、UTF-8文字列の操
// 作として扱う。std::filesystemでのパス結合はWindows
// のルートを再解釈し、呼び出し側の元の区切り表記を変える場合がある。
String ClassicEngine::executePath(StringView appPath, bool engine) const {
    if (enginePath.empty()) return {};

    String path = enginePath;
    if (!utils::pathIsAbsolute(path)) {
        path = utils::buildPath(utils::pathDirName(appPath), path);
    }
    if (!engine && !execute.empty()) {
        if (utils::pathIsAbsolute(execute)) {
            path = execute;
        } else {
            path = utils::buildPath(utils::pathDirName(path), execute);
        }
    }
    return path;
}

String ClassicEngine::legacyName() const {
    const auto name = utils::pathBaseName(enginePath);
    const auto extension = utils::pathExtension(name);
    return String(name.substr(0, name.size() - extension.size()));
}

void LaunchOption::parseStrings(const Array<String>& args) {
    std::size_t sc = 0;
    for (std::size_t i = 0; i < args.size(); ++i) {
        try {
            const auto& option = args[i];
            if (option == "-a") {
                if (i + 1 < args.size()) openPaths.push_back("area:id:" + args[i + 1]);
                sc = i + 2;
                ++i;
            } else if (option == "-b") {
                if (i + 1 < args.size()) openPaths.push_back("battle:id:" + args[i + 1]);
                sc = i + 2;
                ++i;
            } else if (option == "-p") {
                if (i + 1 < args.size()) openPaths.push_back("package:id:" + args[i + 1]);
                sc = i + 2;
                ++i;
            } else if (option == "-conf") {
                if (i + 1 < args.size()) conf = args[i + 1];
                sc = i + 2;
                ++i;
            } else if (option == "-create") {
                create = true;
                if (i + 1 < args.size()) createName = args[i + 1];
                if (i + 2 < args.size()) createSkinType = args[i + 2];
                if (i + 3 < args.size()) createSkinName = args[i + 3];
                sc = i + 4;
                i += 2;
            } else if (option == "-createclassic") {
                createclassic = true;
                if (i + 1 < args.size()) createName = args[i + 1];
                if (i + 2 < args.size()) createclassicPath = args[i + 2];
                sc = i + 3;
                i += 2;
            } else if (option == "-selectfile") {
                if (i + 1 < args.size()) selectfile = args[i + 1];
                sc = i + 2;
                ++i;
            } else if (option == "-putlangfile") {
                if (i + 1 < args.size()) putlangfile = args[i + 1];
                sc = i + 1;
                ++i;
            } else if (option == "-noload") {
                noload = true;
                sc = i + 1;
            } else if (option == "-help" || option == "-h" || option == "/?") {
                help = true;
            } else if (i > sc) {
                openPaths.push_back(option);
            }
        } catch (const std::bad_alloc&) {
            // DのOutOfMemoryErrorはcatch(Exception)の対象外。この境界を維持する。
            throw;
        } catch (const std::length_error&) {
            // ネイティブ文字列の長さ制限は致命的な容量不足であり、不正なコマンドライン入力では
            // ない。これもDの回復可能なcatch(Exception)経路の対象外とする。
            throw;
        } catch (const std::exception&) {
            // Dは不正なコマンドライン値を捕捉して記録し、処理を続ける。
        }
    }
    if (sc < args.size()) scenario = args[sc];
    if (create || createclassic) scenario.clear();
}

String parseDollarParams(StringView format,
                         const Dictionary<char, String>& params) {
    String result;
    result.reserve(format.size());
    bool escaped = false;
    for (const char c : format) {
        if (escaped) {
            const char lower = static_cast<char>(
                std::tolower(static_cast<unsigned char>(c)));
            const char upper = static_cast<char>(
                std::toupper(static_cast<unsigned char>(c)));
            if (const auto lowerIt = params.find(lower); lowerIt != params.end()) {
                result += lowerIt->second;
            } else if (const auto upperIt = params.find(upper); upperIt != params.end()) {
                result += upperIt->second;
            } else if (c == '$') {
                result.push_back('$');
            } else {
                result.push_back('$');
                result.push_back(c);
            }
            escaped = false;
        } else if (c == '$') {
            escaped = true;
        } else {
            result.push_back(c);
        }
    }
    if (escaped) result.push_back('$');
    return result;
}

namespace {

template <typename Enum>
[[nodiscard]] std::size_t enumIndex(Enum value) {
    return static_cast<std::size_t>(value);
}

template <typename Enum, std::size_t N>
[[nodiscard]] String nameAt(Enum value,
                            const std::array<StringView, N>& names,
                            StringView kind) {
    const auto index = enumIndex(value);
    if (index >= names.size()) {
        throw std::invalid_argument("Unknown " + String(kind) + " value");
    }
    return String(names[index]);
}

template <typename Enum, std::size_t N>
[[nodiscard]] Enum enumFromName(StringView name,
                                 const std::array<StringView, N>& names,
                                 StringView kind) {
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i] == name) return static_cast<Enum>(i);
    }
    throw std::invalid_argument("Unknown " + String(kind) + " name: " + String(name));
}

}  // namespace

String fromSex(Sex e) {
    static constexpr std::array<StringView, 2> names{"Male", "Female"};
    return nameAt(e, names, "sex");
}

Sex toSex(StringView e) {
    static constexpr std::array<StringView, 2> names{"Male", "Female"};
    return enumFromName<Sex>(e, names, "sex");
}

String fromPeriod(Period e) {
    static constexpr std::array<StringView, 4> names{"Child", "Young", "Adult", "Old"};
    return nameAt(e, names, "period");
}

Period toPeriod(StringView e) {
    static constexpr std::array<StringView, 4> names{"Child", "Young", "Adult", "Old"};
    return enumFromName<Period>(e, names, "period");
}

String fromNature(Nature e) {
    static constexpr std::array<StringView, 12> names{
        "Spi", "Agl", "Str", "Vit", "Int", "Sch",
        "Med", "Bri", "Mat", "Gen", "Her", "Div"};
    return nameAt(e, names, "nature");
}

Nature toNature(StringView e) {
    static constexpr std::array<StringView, 12> names{
        "Spi", "Agl", "Str", "Vit", "Int", "Sch",
        "Med", "Bri", "Mat", "Gen", "Her", "Div"};
    return enumFromName<Nature>(e, names, "nature");
}

String fromMakings(Makings e) {
    static constexpr std::array<StringView, 48> names{
        "LooksB", "LooksU", "ClassH", "ClassL", "BredT", "BredC",
        "MeansH", "MeansL", "FaithF", "FaithI", "ReliR", "ReliU",
        "DispC", "DispS", "DesireG", "DesireC", "DevoteD", "DevoteS",
        "DiscO", "DiscC", "PolitR", "PolitC", "SenseR", "SenseS",
        "CurioB", "CurioI", "NotionR", "NotionM", "IdeaO", "IdeaP",
        "WorkH", "WorkS", "CharC", "CharB", "StyleF", "StyleP",
        "PrideP", "PrideM", "RefR", "RefB", "GraceG", "GraceR",
        "LinerH", "LinerM", "PerS", "PerT", "FameH", "FameA"};
    return nameAt(e, names, "makings");
}

Makings toMakings(StringView e) {
    static constexpr std::array<StringView, 48> names{
        "LooksB", "LooksU", "ClassH", "ClassL", "BredT", "BredC",
        "MeansH", "MeansL", "FaithF", "FaithI", "ReliR", "ReliU",
        "DispC", "DispS", "DesireG", "DesireC", "DevoteD", "DevoteS",
        "DiscO", "DiscC", "PolitR", "PolitC", "SenseR", "SenseS",
        "CurioB", "CurioI", "NotionR", "NotionM", "IdeaO", "IdeaP",
        "WorkH", "WorkS", "CharC", "CharB", "StyleF", "StyleP",
        "PrideP", "PrideM", "RefR", "RefB", "GraceG", "GraceR",
        "LinerH", "LinerM", "PerS", "PerT", "FameH", "FameA"};
    return enumFromName<Makings>(e, names, "makings");
}

// event.hppがCDetailとコンテント種別の検索表を持つた
// め、これらのメソッドは実装ファイルに置き、ヘッダーの循環を避ける。
XNode ContentConversionGroup::toNode() const {
    auto e = XNode::create(XML_NAME, String{});
    toNodeImpl(e);
    return e;
}

void ContentConversionGroup::toNode(XNode& node, String name) const {
    auto e = node.newElement(name, String{});
    toNodeImpl(e);
}

void ContentConversionGroup::toNodeImpl(XNode& node) const {
    for (const CType cType : group) {
        const auto detail = event::contentDetail(cType);
        node.newElement(detail.names.front(), detail.type);
    }
}

void ContentConversionGroup::fromNode(XNode& node) {
    group.clear();
    node.onTag["type"] = [this](XNode& child) {
        group.push_back(event::cTypeFrom(
            child.value(), child.attr<String>("type", false, "")));
    };
    node.parse();
}

ToolBarSettings ToolBarSettings::opCall(Array<Array<Tool>> tools) {
    ToolBarSettings result;
    result.tools = std::move(tools);
    return result;
}

namespace {

String menuIdName(const types::MenuID value) {
    for (const auto member : enumutils::enumMembers<types::MenuID>()) {
        if (member.value == value) return String(member.name);
    }
    throw std::invalid_argument("unknown MenuID");
}

}  // namespace

XNode Tool::toNode() const {
    auto element = XNode::create(XML_NAME, separator ? String{} : menuIdName(menu));
    toNodeImpl(element);
    return element;
}

void Tool::toNode(XNode& node) const {
    auto element = node.newElement(XML_NAME, separator ? String{} : menuIdName(menu));
    toNodeImpl(element);
}

void Tool::fromNode(XNode& node) {
    separator = node.attr<bool>("separator", false, false);
    if (separator) return;
    try {
        menu = node.valueTo<types::MenuID>();
    } catch (const xml::XmlValueError&) {
        // Dは変換失敗時に以前のメニューを保持する。有効なNone
        // メンバーは通常の項目であり、暗黙の区切りではない。
        separator = true;
    }
}

ToolBarSettings ToolBarSettings::dup() const {
    ToolBarSettings result;
    result.tools = tools;
    return result;
}

void ToolBarSettings::toNode(XNode& e, String name) const {
    auto node = e.newElement(name, String{});
    for (const auto& toolbar : tools) {
        auto bar = node.newElement("toolBar", String{});
        for (const auto& tool : toolbar) tool.toNode(bar);
    }
}

void ToolBarSettings::fromNode(XNode& node) {
    tools.clear();
    node.onTag["toolBar"] = [this](XNode& bar) {
        Array<Tool> toolbar;
        bar.onTag[String(Tool::XML_NAME)] = [&toolbar](XNode& item) {
            Tool tool;
            tool.fromNode(item);
            toolbar.push_back(std::move(tool));
        };
        bar.parse();
        tools.push_back(std::move(toolbar));
    };
    node.parse();
}

}  // namespace cwxeditor::cwx::structs
