#include "cwxeditor_src/core/cwx/skin.hpp"

#include "cwxeditor_src/core/cwx/imagesize.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/enumutils.hpp"
#include "cwxeditor_src/core/cwx/sjis.hpp"
#include "cwxeditor_src/lib/d_regex.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <new>
#include <sstream>
#include <stdexcept>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace cwxeditor::cwx::skin {

namespace {

using Path = std::filesystem::path;
using StringArray = ::cwxeditor::lib::Array<String>;

Path fsPath(StringView value) {
    if (value.empty()) return {};
    const auto* first = reinterpret_cast<const char8_t*>(value.data());
    return Path(std::u8string(first, value.size()));
}

String utf8Path(const Path& value) {
    const auto result = value.generic_u8string();
    return String(reinterpret_cast<const char*>(result.data()), result.size());
}

bool exists(StringView value) {
    return utils::pathExists(fsPath(value));
}

bool isDirectory(StringView value) {
    return utils::pathIsDirectory(fsPath(value));
}

String baseName(StringView value) { return String(utils::pathBaseName(value)); }
String dirName(StringView value) { return String(utils::pathDirName(value)); }
String extension(StringView value) { return fsPath(value).extension().generic_string(); }
String stripExtension(StringView value) {
    return utils::setPathExtension(value, {});
}
String setExtension(StringView value, StringView ext) {
    return utils::setPathExtension(value, ext);
}
String joinPath(StringView left, StringView right) {
    return utils::buildPath(left, right);
}

String lower(StringView value) {
    String result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

String readText(StringView filename) {
    const auto bytes = utils::readBinary(fsPath(filename));
    const String raw(bytes.begin(), bytes.end());
    // std.file.readTextはUTF-8を検証し、UTF-8のBOMを保持する。UTF-16/32
    // のBOMもここでは不正なUTF-8となる。UTFExceptionはinvalid_argument
    // へ対応させる。Dの従来文字コードでの別の再試行を持つのはloadEngineSettingsだけ。
    enumutils::validateUtf8(raw);
    return raw;
}

StringView enhanceSuffix(Enhance value) {
    switch (value) {
    case Enhance::Action: return "0";
    case Enhance::Avoid: return "1";
    case Enhance::Resist: return "2";
    case Enhance::Defense: return "3";
    default:
        // Dのdefaultでのアサートを、ネイティブの不変条件例外へ変換する。
        throw std::logic_error("Invalid Enhance resource value");
    }
}

StringView mentalityResourceName(Mentality value) {
    switch (value) {
    case Mentality::Normal: return "MIND0";
    case Mentality::Sleep: return "MIND1";
    case Mentality::Confuse: return "MIND2";
    case Mentality::Overheat: return "MIND3";
    case Mentality::Brave: return "MIND4";
    case Mentality::Panic: return "MIND5";
    default:
        // Dのdefaultでのアサートを、ネイティブの不変条件例外へ変換する。
        throw std::logic_error("Invalid Mentality resource value");
    }
}

const XMLInfo& requiredXMLInfo(const ::cwxeditor::lib::Owner<const XMLInfo>& value) {
    // ネイティブのモデルファクトリーはXMLInfoへの参照を必要とする。null
    // の所有元を参照せず、その境界でDのクラスの状態がないことを報告する。
    if (!value) throw std::invalid_argument("Missing XMLInfo context");
    return *value;
}

::cwxeditor::lib::Array<Nature> selectNatures(const Skin::NatureData& data, bool special) {
    ::cwxeditor::lib::Array<Nature> result;
    if (data.empty()) {
        const std::size_t first = special ? 6U : 0U;
        for (auto index = first; index < first + 6U; ++index) result.emplace_back(index);
    } else {
        // Dは連想配列の巡回順で追加する。ネイティブunordered_map
        // も未規定の順序を持つため、独自の並べ替え規則を追加しない。
        for (const auto& [key, value] : data) {
            // Dのnullクラスへのアクセスは、ネイティブの不変条件例外へ変換する。
            if (!value) throw std::logic_error("Missing nature modifier data");
            if (value->special() == special) result.push_back(key);
        }
    }
    return result;
}

::cwxeditor::lib::DRegex compileDRegex(StringView pattern, bool ignoreCase) {
    auto decoded = enumutils::toUtf32(pattern);
    // Dはファイル名の大文字小文字の規則を明示的に選ぶ。共通検索アダ
    // プターの既定は区別なしなので、区別するホストでは設定し直す。
    decoded.insert(0, ignoreCase ? U"(?i)" : U"(?-i)");
    // Dの正規表現アダプターはdchar値を照合し、Phobosのケースフォールドを適用す
    // る。UTF-8のstd::stringに対するstd::regexはバイトを文字と
    // みなすため、非ASCIIのEngine名のi指定パターンが大文字表記に一致しない。
    return ::cwxeditor::lib::DRegex(decoded);
}

bool regexSearch(StringView value, const ::cwxeditor::lib::DRegex& expression) {
    return !expression.matchAll(enumutils::toUtf32(value)).empty();
}

bool regexMatch(StringView value, StringView pattern) {
    if (pattern.empty()) return false;
    const auto expression = compileDRegex(pattern, true);
    return regexSearch(value, expression);
}

bool isBinaryImage(StringView value) { return utils::isBinImg(value); }

std::uint32_t enumValue(types::Enhance value) {
    return static_cast<std::uint32_t>(value);
}

String warning(const props::CProps& prop, StringView key) {
    return detail::warningMessage(prop.requiredMsgs(), key);
}

template <typename T>
void setAttr(XNode& node, StringView name, const T& value) {
    // 文字列のオーバーロードを明示的に呼び、MSVCがStringやStringView
    // でテンプレートのnew_attrを再帰的に選ぶのを避ける。
    node.new_attr(name, utils::detail::debugValue(value));
}

void setAttr(XNode& node, StringView name, StringView value) {
    node.new_attr(name, value);
}

String codepointToUtf8(char32_t value) {
    String result;
    if (value <= 0x7f) result.push_back(static_cast<char>(value));
    else if (value <= 0x7ff) {
        result.push_back(static_cast<char>(0xc0 | (value >> 6)));
        result.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else if (value <= 0xffff) {
        result.push_back(static_cast<char>(0xe0 | (value >> 12)));
        result.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        result.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else {
        result.push_back(static_cast<char>(0xf0 | (value >> 18)));
        result.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
        result.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        result.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    }
    return result;
}

char32_t upperCodepoint(const char32_t value) noexcept {
    return enumutils::upperScalar(value);
}

String extensionLower(StringView value) { return lower(extension(value)); }

template <typename Predicate>
StringArray listDirectory(StringView directory, Predicate predicate) {
    StringArray result;
    for (const auto& child : utils::clistdir(fsPath(directory))) {
        const auto path = joinPath(directory, child);
        if (predicate(path)) result.push_back(baseName(path));
    }
    return result;
}

template <int Kind>
bool matchesResource(const Skin& skin, StringView path, bool flag, bool flag2) {
    if constexpr (Kind == 0) return skin.isCardImage(path, flag, flag2);
    else if constexpr (Kind == 1) return skin.isBgImage(path, flag);
    else if constexpr (Kind == 2) return skin.isBGM(path);
    else return skin.isSE(path);
}

template <int Kind>
struct ResourcePresence {
    bool flag{}, flag2{}, result{};
};

template <int Kind>
bool hasResource(const Skin& skin, StringView dir, bool forceRefresh,
                 bool flag, bool flag2) {
    utils::FileCache<ResourcePresence<Kind>> cache;
    const auto directory = fsPath(dir);
    if (!forceRefresh) {
        if (const auto* saved = cache.cache(directory);
            saved && saved->value.flag == flag && saved->value.flag2 == flag2)
            return saved->value.result;
    }
    bool result = false;
    if (exists(dir) && isDirectory(dir)) {
        // DのDirEntryは、暗黙に完全な名前を判定関数へ
        // 渡す。遅延列挙を維持し、次の項目を読む前に停止する。
        for (utils::ShallowDirectory entries(directory); !entries.empty(); entries.popFront()) {
            const auto entry = entries.front();
            if (matchesResource<Kind>(skin, entry.name(), flag, flag2)) {
                result = true;
                break;
            }
        }
    }
    cache.putCache(directory, ResourcePresence<Kind>{flag, flag2, result}, 0);
    return result;
}

template <int Kind>
struct ResourceFiles {
    bool flag{}, flag2{};
    ::cwxeditor::lib::SharedSlice<String> files;
};

template <int Kind>
::cwxeditor::lib::SharedSlice<String> listResourceFiles(
    const Skin& skin, StringView dir, bool forceRefresh, bool flag, bool flag2) {
    utils::FileCache<ResourceFiles<Kind>> cache;
    const auto directory = fsPath(dir);
    if (!forceRefresh) {
        if (const auto* saved = cache.cache(directory);
            saved && saved->value.flag == flag && saved->value.flag2 == flag2)
            return saved->value.files;
    }
    StringArray result;
    for (const auto& file : utils::clistdir(directory)) {
        const String path = joinPath(dir, file);
        if (matchesResource<Kind>(skin, path, flag, flag2)) result.push_back(baseName(path));
    }
    // Dの配列記述子をコピーし、キャッシュと戻り値の両方で領
    // 域を保持する。キャッシュした要素の領域はコピーしない。
    ::cwxeditor::lib::SharedSlice<String> files(std::move(result));
    cache.putCache(directory, ResourceFiles<Kind>{flag, flag2, files}, 0);
    return files;
}

}  // namespace

// 両方向の変換で意図的に平滑化設定を保持する。Dは既存のBgImageS
// のこの項目を省略し、既定以外の画像・PC設定をリセットしていた。
::cwxeditor::lib::Array<BgImageS> createBgImageSs(const BgImageArray& bgs) {
    ::cwxeditor::lib::Array<BgImageS> result;
    result.reserve(bgs.size());
    for (const auto& bg : bgs) {
        if (!bg) continue;
        BgImageS value;
        value.x = bg->x(); value.y = bg->y(); value.width = static_cast<std::uint32_t>(bg->width());
        value.height = static_cast<std::uint32_t>(bg->height()); value.mask = bg->mask();
        value.layer = bg->layer(); value.cellName = bg->cellName();
        if (const auto image = std::dynamic_pointer_cast<background::ImageCell>(bg)) {
            value.type = "image"; value.name = stripExtension(image->path());
            value.smoothing = image->smoothing();
        } else if (const auto text = std::dynamic_pointer_cast<background::TextCell>(bg)) {
            value.type = "text"; value.text = text->text(); value.fontName = text->fontName();
            value.size = text->size(); value.color = text->color(); value.bold = text->bold();
            value.italic = text->italic(); value.underline = text->underline();
            value.strike = text->strike(); value.vertical = text->vertical();
            value.antialias = text->antialias(); value.borderingType = text->borderingType();
            value.borderingColor = text->borderingColor(); value.borderingWidth = text->borderingWidth();
            value.updateType = text->updateType();
        } else if (const auto color = std::dynamic_pointer_cast<background::ColorCell>(bg)) {
            value.type = "color"; value.blendMode = color->blendMode();
            value.gradientDir = color->gradientDir(); value.color1 = color->color1();
            value.color2 = color->color2();
        } else if (const auto pc = std::dynamic_pointer_cast<background::PCCell>(bg)) {
            value.type = "pc"; value.pcNumber = pc->pcNumber(); value.expand = pc->expand();
            value.smoothing = pc->smoothing();
        }
        result.push_back(std::move(value));
    }
    return result;
}

BgImageArray createBgImages(const Skin& skin, const ::cwxeditor::lib::Array<BgImageS>& bgs) {
    BgImageArray result;
    result.reserve(bgs.size());
    for (const auto& value : bgs) {
        ::cwxeditor::lib::Owner<BgImage> cell;
        if (value.type == "image") {
            std::size_t index = 0;
            String path = skin.findImagePath(setExtension(value.name, ".bmp"), {},
                                              versioninfo::LATEST_VERSION, index);
            if (!path.empty()) {
                const auto dirs = skin.tableDirs();
                if (index < dirs.size()) {
                    const String absolutePath = utils::nabs(StringView(path));
                    const String absoluteDir = utils::nabs(StringView(dirs[index]));
                    path = utils::abs2rel(StringView(absolutePath), StringView(absoluteDir));
                }
            } else path = setExtension(value.name, ".bmp");
            cell = std::make_shared<background::ImageCell>(std::move(path), String{}, value.x,
                value.y, static_cast<std::int32_t>(value.width), static_cast<std::int32_t>(value.height), value.mask);
            std::dynamic_pointer_cast<background::ImageCell>(cell)->smoothing(value.smoothing);
        } else if (value.type == "text") {
            cell = std::make_shared<background::TextCell>(value.text, value.fontName, value.size,
                value.color, value.bold, value.italic, value.underline, value.strike, value.vertical,
                value.antialias, value.borderingType, value.borderingColor, value.borderingWidth,
                value.updateType, String{}, value.x, value.y, static_cast<std::int32_t>(value.width),
                static_cast<std::int32_t>(value.height), value.mask);
        } else if (value.type == "color") {
            cell = std::make_shared<background::ColorCell>(value.blendMode, value.gradientDir,
                value.color1, value.color2, String{}, value.x, value.y,
                static_cast<std::int32_t>(value.width), static_cast<std::int32_t>(value.height), value.mask);
        } else if (value.type == "pc") {
            cell = std::make_shared<background::PCCell>(value.pcNumber, value.expand, String{},
                value.x, value.y, static_cast<std::int32_t>(value.width),
                static_cast<std::int32_t>(value.height), value.mask);
            std::dynamic_pointer_cast<background::PCCell>(cell)->smoothing(value.smoothing);
        } else {
            throw std::runtime_error("Unknown type: " + value.type);
        }
        cell->layer(value.layer); cell->cellName(value.cellName); result.push_back(std::move(cell));
    }
    return result;
}

String skinTypeByClassicEngine(const ::cwxeditor::lib::Array<SkinTypeByClassicEngine>& info,
                               StringView enginePath, StringView defaultType) {
    const auto engine = baseName(enginePath); const auto directory = baseName(dirName(enginePath));
    for (const auto& entry : info) {
        try { if (regexMatch(engine, entry.engine)) return entry.type; } catch (...) {}
        try { if (regexMatch(directory, entry.directory)) return entry.type; } catch (...) {}
    }
    return String(defaultType);
}

Skin::Ptr Skin::find(const CProps& prop, StringView enginePath, StringView type,
                     StringView scenarioPath, bool legacy,
                     StringView classicEngineRegex, StringView classicDataDirRegex,
                     StringView classicMatchKey,
                     const ::cwxeditor::lib::Array<ClassicEngine>& engines,
                     StringView defaultSkin) {
    if (legacy && type.empty()) {
        return findLegacySkin(prop, enginePath, scenarioPath, classicEngineRegex,
                              classicDataDirRegex, classicMatchKey, engines, defaultSkin);
    }
    const auto skins = table(prop, enginePath);
    // 組み込みの既定値は共通のスラッシュを使う場合があるが、表にはネイティブのSkin.xml
    // パスを保存する。区切り文字の表記ではなく、ホストのファイル名の同一性で照合する。
    const auto preferred = std::find_if(skins.begin(), skins.end(), [&](const auto& entry) {
        return utils::cfnmatch(entry.first, prop.defaultSkinPath());
    });
    if (preferred != skins.end() && preferred->second &&
        (type.empty() || preferred->second->type() == type)) {
        preferred->second->initialize();
        return preferred->second;
    }
    // 同じ種別の複数のスキンを利用できるよう、表はSkin.xml
    // パスをキーにする。種別の文字列では通常のスキン（MedievalFantasy/Classic
    // など）を検索できず、Table資源を持たな
    // い空のSkinへ黙って切り替わってしまう。
    for (const auto& [path, candidate] : skins) {
        static_cast<void>(path);
        if (!candidate || candidate->type() != type) continue;
        candidate->initialize();
        return candidate;
    }
    return emptySkin(prop, enginePath, type);
}

Skin::Ptr Skin::emptySkin(const CProps& prop, StringView enginePath, StringView type) {
    // Dは要求された種別に関係なく、Engineごとに一つの空の代替スキンを再利用する。
    // table()が使うネイティブのセッションキャッシュ内でも、その同一性を維持する。
    (void)type;
    const String engine = enginePath.empty() ? String{} : utils::nabs(enginePath);
    const auto& key = engine;
    std::scoped_lock cache_lock(prop.skin_tables_mutex_);
    if (const auto found = prop.empty_skins_.find(key); found != prop.empty_skins_.end())
        return found->second;
    auto fallback = std::make_shared<Skin>(prop, enginePath);
    fallback->initialize();
    prop.empty_skins_.emplace(key, fallback);
    return fallback;
}

Skin::Table Skin::table(const CProps& prop, StringView enginePath) {
    // 組み込みALLは、Dの外部EngineパスがなくてもUserData
    // のスキンを探索する。CProps所有のキャッシュにより、そのSkin
    // 参照の範囲を、パスを渡したEditorセッション内に限定する。
    const bool hosted = !prop.defaultSkinPath().empty();
    if (!hosted && (enginePath.empty() || !exists(enginePath))) return {};
    const String absolute = enginePath.empty() ? String{} : utils::nabs(StringView(enginePath));
    // Dと同様に各構成要素を結合し、Skin表のキーにネイティブの区切り文字を使う。
    const String skinsDir = hosted ? dirName(dirName(prop.defaultSkinPath()))
                                  : joinPath(dirName(absolute), joinPath("Data", "Skin"));
    const auto key = std::make_pair(absolute, skinsDir);
    std::scoped_lock cache_lock(prop.skin_tables_mutex_);
    if (const auto found = prop.skin_tables_.find(key); found != prop.skin_tables_.end())
        return found->second;
    Table result;
    if (exists(skinsDir) && isDirectory(skinsDir)) {
        try {
            for (const auto& child : utils::clistdir(fsPath(skinsDir))) {
                const String directory = joinPath(skinsDir, child);
                if (!isDirectory(directory)) continue;
                const String filename = joinPath(directory, "Skin.xml");
                if (!exists(filename)) continue;
                try {
                    Ptr skin(new Skin(prop, filename, absolute));
                    result.emplace(filename, std::move(skin));
                } catch (...) {
                    // 不正な任意のスキンによって、他のスキンを見えなくしない。
                }
            }
        } catch (...) {
            // ディレクトリ列挙でエラーが出た場合は、それまでに見つかったスキンを返す。
        }
    }
    prop.skin_tables_.emplace(key, result);
    return result;
}

Skin::Ptr Skin::createLegacySkin(const CProps& prop, StringView enginePath,
                                 StringView legacyEnginePath, StringView dataDirName,
                                 StringView execute,
                                 const ::cwxeditor::lib::Array<ClassicEngine>& engines,
                                 StringView defaultSkin) {
    String engine = enginePath.empty() ? String{} : prop.toAppAbs(enginePath);
    String legacyEngine = legacyEnginePath.empty() ? String{} : utils::nabs(StringView(prop.toAppAbs(legacyEnginePath)));
    const auto skins = table(prop, engine);
    const auto selected = skins.find(String(defaultSkin));
    // 共通の表はSkin.xmlパスをキーとするが、設定は種別を指定す
    // る場合がある。通常の選択処理を再利用し、旧形式のシナリオも空の基
    // 底スキンを作るのではなく、同じ設定済みの既定スキンを継承する。
    const auto base = selected != skins.end() ? selected->second
        : find(prop, engine, defaultSkin, {}, false, {}, {}, {}, {}, defaultSkin);
    ClassicEngine classic;
    for (const auto& candidate : engines) {
        if (utils::cfnmatch(utils::nabs(StringView(prop.toAppAbs(candidate.enginePath))), StringView(legacyEngine))) {
            classic = candidate.dup(); break;
        }
    }
    Ptr result = base && !base->skinFile().empty() ? Ptr(new Skin(prop, base->skinFile(), engine))
                                                  : std::make_shared<Skin>(prop, engine);
    result->setupLegacy(legacyEngine, joinPath(dirName(legacyEngine), dataDirName), classic);
    result->execute_ = String(execute);
    return result;
}

Skin::Ptr Skin::findLegacySkin(const CProps& prop, StringView enginePath,
                               StringView scenarioPath, StringView classicEngineRegex,
                               StringView classicDataDirRegex, StringView classicMatchKey,
                               const ::cwxeditor::lib::Array<ClassicEngine>& engines,
                               StringView defaultSkin) {
    String resource, legacyEngine;
    findLegacy(scenarioPath, resource, legacyEngine, classicEngineRegex,
               classicDataDirRegex, classicMatchKey, engines);
    if (!resource.empty()) resource = utils::nabs(StringView(resource));
    if (!legacyEngine.empty()) legacyEngine = utils::nabs(StringView(legacyEngine));
    const String dataDir = resource.empty() ? String{} : utils::abs2rel(StringView(resource), StringView(dirName(legacyEngine)));
    return createLegacySkin(prop, enginePath, legacyEngine, dataDir, {}, engines, defaultSkin);
}

void Skin::setupLegacy(StringView legacyEnginePath, StringView resourceDir,
                       const ClassicEngine& engine) {
    classic_engine_ = engine;
    legacy_ = true; legacy_path_ = String(resourceDir); legacy_engine_ = String(legacyEnginePath);
    for (const char32_t key : {U'A', U'B', U'D', U'E', U'F', U'G', U'H', U'J', U'K', U'L',
                               U'N', U'O', U'P', U'Q', U'S', U'W', U'X', U'Z'}) {
        sp_chars_.try_emplace(key, String{});
    }
}

bool Skin::canAccess(StringView file) {
    return utils::canAccessPath(file);
}

String Skin::findResDir(StringView path, StringView classicDataDirRegex,
                        StringView classicMatchKey) {
    const auto expression = compileDRegex(
        classicDataDirRegex, utils::fncmp("A", "a") == 0);
    for (const auto& directory : utils::clistdir(fsPath(path))) {
        const String candidate = joinPath(path, directory);
        if (!canAccess(candidate)) continue;
        try {
            if (isDirectory(candidate) && regexSearch(directory, expression) &&
                exists(joinPath(candidate, classicMatchKey))) {
                return directory;
            }
        } catch (const std::bad_alloc&) {
            // Dのcatch(Exception)はOutOfMemoryErrorを捕捉しない。
            throw;
        } catch (const std::exception& error) { utils::fdebugln(error.what()); }
    }
    return {};
}

bool Skin::hasClassicEngine(StringView path, String& resourceDir, String& enginePath,
                            StringView classicEngineRegex, StringView classicDataDirRegex,
                            StringView classicMatchKey,
                            const ::cwxeditor::lib::Array<ClassicEngine>& engines) {
    resourceDir.clear(); enginePath.clear();
    for (const auto& engine : engines) {
        const String name = baseName(engine.enginePath);
        const String dataDir = engine.dataDirName;
        const String dataPath = fsPath(dataDir).is_absolute()
            ? dataDir : joinPath(path, dataDir);
        // Dは明示設定された絶対パスのデータディレクトリを正とする。相対デ
        // ィレクトリだけが、候補のインストール先の下に存在する必要がある。
        if (fsPath(dataDir).is_absolute() || exists(dataPath)) {
            const String candidate = joinPath(path, name);
            if (exists(candidate)) { enginePath = candidate; resourceDir = dataPath; return true; }
        }
    }
    const String directory = findResDir(path, classicDataDirRegex, classicMatchKey);
    if (directory.empty()) return false;
    resourceDir = joinPath(path, directory);
    // Dは項目を調べる前に、実行ファイルの検索式を一度コンパイル
    // する。不正なユーザー設定が分かるようにし、下記のファイルご
    // との可能な範囲の検査内で正規表現エラーを握りつぶさない。
    const auto engineExpression = compileDRegex(
        classicEngineRegex, utils::fncmp("A", "a") == 0);
    for (const auto& file : utils::clistdir(fsPath(path))) {
        const String candidate = joinPath(path, file);
        if (!canAccess(candidate)) continue;
        try {
            if (!isDirectory(candidate) && regexSearch(file, engineExpression)) {
                enginePath = candidate; return true;
            }
        } catch (const std::bad_alloc&) {
            // Dのcatch(Exception)はOutOfMemoryErrorを捕捉しない。
            throw;
        } catch (const std::exception&) {
            // Dのcatch(Exception)に合わせ、通常の項目エラーは無視する。
        }
    }
    resourceDir.clear(); enginePath.clear(); return false;
}

bool Skin::findLegacy(StringView scenarioPath, String& resourceDir, String& enginePath,
                      StringView classicEngineRegex, StringView classicDataDirRegex,
                      StringView classicMatchKey,
                      const ::cwxeditor::lib::Array<ClassicEngine>& engines) {
    String path = dirName(scenarioPath);
    while (!hasClassicEngine(path, resourceDir, enginePath, classicEngineRegex,
                             classicDataDirRegex, classicMatchKey, engines)) {
        const String old = path; path = dirName(path);
        if (old == path) { resourceDir.clear(); enginePath.clear(); return false; }
    }
    return true;
}

String Skin::findCardWirthPy(StringView scenarioPath, StringView exeName,
                             StringView dataName) {
    String path = dirName(scenarioPath);
    for (;;) {
        if (exists(joinPath(path, exeName)) && exists(joinPath(path, dataName)))
            return joinPath(path, exeName);
        const String old = path; path = dirName(path); if (old == path) return {};
    }
}

Skin::Skin(const CProps& prop, StringView enginePath)
    : context_(prop.skin_context_), engine_path_(enginePath) {}

Skin::Skin(const CProps& prop, StringView skinFile, StringView enginePath)
    : Skin(prop, enginePath) {
    if (!skinFile.empty()) loadFromXML(prop, skinFile,
        XMLInfo(*context_->system, versioninfo::LATEST_VERSION));
}

String Skin::type() const { return type_.empty() ? classic_engine_.type : type_; }
String Skin::engine() const { return legacy_engine_.empty() ? engine_path_ : legacy_engine_; }
String Skin::executeEngine() const {
    if (legacy_engine_.empty()) return engine_path_;
    return execute_.empty() ? legacy_engine_ : joinPath(dirName(legacy_engine_), execute_);
}
String Skin::legacyName() const { return legacy_engine_.empty() ? String{} : stripExtension(baseName(legacy_engine_)); }

String Skin::findResource(StringView dir, StringView name, const StringArray& extensions) {
    for (const auto& ext : extensions) {
        const String path = setExtension(joinPath(dir, name), ext);
        if (exists(path)) return path;
    }
    return {};
}

String Skin::resSummary(MaskType& mask) const {
    mask = MaskType::NoMask;
    for (const auto& directory : tableDirs()) {
        const String result = findResource(directory, "Bill", ext_img_);
        if (!result.empty()) return result;
    }
    return {};
}

#define CWX_SKIN_RES_METHOD(method, subdir, filename, mask_value) \
String Skin::method(MaskType& mask) const { mask = MaskType::mask_value; return findResource(resourceDir(), subdir "/" filename, ext_img_); }

CWX_SKIN_RES_METHOD(resMenuCard, "CardBg", "NORMAL", NoMask)
CWX_SKIN_RES_METHOD(resCastCard, "CardBg", "LARGE", NoMask)
CWX_SKIN_RES_METHOD(resCastCardInjury, "CardBg", "INJURY", NoMask)
CWX_SKIN_RES_METHOD(resCastCardDanger, "CardBg", "DANGER", NoMask)
CWX_SKIN_RES_METHOD(resCastCardFaint, "CardBg", "FAINT", NoMask)
CWX_SKIN_RES_METHOD(resCastCardBind, "CardBg", "BIND", NoMask)
CWX_SKIN_RES_METHOD(resCastCardParaly, "CardBg", "PARALY", NoMask)
CWX_SKIN_RES_METHOD(resCastCardPetrif, "CardBg", "PETRIF", NoMask)
CWX_SKIN_RES_METHOD(resCastCardSleep, "CardBg", "SLEEP", NoMask)
CWX_SKIN_RES_METHOD(resLifeBar, "Status", "LIFEBAR", NoMask)
CWX_SKIN_RES_METHOD(resLifeGuage, "Status", "LIFEGUAGE", NoMask)
CWX_SKIN_RES_METHOD(resLifeGuage2, "Status", "LIFEGUAGE2", NormalMask)
CWX_SKIN_RES_METHOD(resLifeGuage2Mask, "Status", "LIFEGUAGE2_MASK", NormalMask)
CWX_SKIN_RES_METHOD(resBind, "Status", "MAGIC0", NoMask)
CWX_SKIN_RES_METHOD(resSilence, "Status", "MAGIC1", NoMask)
CWX_SKIN_RES_METHOD(resFaceUp, "Status", "MAGIC2", NoMask)
CWX_SKIN_RES_METHOD(resAntiMagic, "Status", "MAGIC3", NoMask)
CWX_SKIN_RES_METHOD(resParalyze, "Status", "BODY1", NoMask)
CWX_SKIN_RES_METHOD(resPoison, "Status", "BODY0", NoMask)
CWX_SKIN_RES_METHOD(resSummon, "Status", "SUMMON", NoMask)
CWX_SKIN_RES_METHOD(resItemCard, "CardBg", "ITEM", NoMask)
CWX_SKIN_RES_METHOD(resSkillCard, "CardBg", "SKILL", NoMask)
CWX_SKIN_RES_METHOD(resBeastCard, "CardBg", "BEAST", NoMask)
CWX_SKIN_RES_METHOD(resOptionCard, "CardBg", "OPTION", NoMask)
CWX_SKIN_RES_METHOD(resInfoCard, "CardBg", "INFO", NoMask)
CWX_SKIN_RES_METHOD(resCardHold, "CardBg", "HOLD", NoMask)
CWX_SKIN_RES_METHOD(resCardPenalty, "CardBg", "PENALTY", NoMask)
CWX_SKIN_RES_METHOD(resRare, "CardBg", "RARE", RightMask)
CWX_SKIN_RES_METHOD(resPremier, "CardBg", "PREMIER", RightMask)
CWX_SKIN_RES_METHOD(resAptVeryHigh, "Stone", "HAND3", NormalMask)
CWX_SKIN_RES_METHOD(resAptHigh, "Stone", "HAND2", NormalMask)
CWX_SKIN_RES_METHOD(resAptNormal, "Stone", "HAND1", NormalMask)
CWX_SKIN_RES_METHOD(resAptLow, "Stone", "HAND0", NormalMask)
CWX_SKIN_RES_METHOD(resUse0, "Stone", "HAND5", NormalMask)
CWX_SKIN_RES_METHOD(resUse1, "Stone", "HAND6", NormalMask)
CWX_SKIN_RES_METHOD(resUse2, "Stone", "HAND7", NormalMask)
CWX_SKIN_RES_METHOD(resUse3, "Stone", "HAND8", NormalMask)
CWX_SKIN_RES_METHOD(resUse4, "Stone", "HAND9", NormalMask)

#undef CWX_SKIN_RES_METHOD

String Skin::resEnhanceUp(MaskType& mask, Enhance value) const {
    mask = MaskType::Mask1_1;
    const String name = "UP" + String(enhanceSuffix(value));
    return findResource(resourceDir(), utils::buildPath("Status", name), ext_img_);
}
String Skin::resEnhanceDown(MaskType& mask, Enhance value) const {
    mask = MaskType::Mask1_1;
    const String name = "DOWN" + String(enhanceSuffix(value));
    return findResource(resourceDir(), utils::buildPath("Status", name), ext_img_);
}
String Skin::resMentality(MaskType& mask, Mentality value) const {
    mask = MaskType::NoMask;
    const auto name = mentalityResourceName(value);
    return findResource(resourceDir(), utils::buildPath("Status", name), ext_img_);
}

::cwxeditor::lib::Dictionary<String, String> Skin::loadEngineSettings() const {
    ::cwxeditor::lib::Dictionary<String, String> result;
    if (!legacy_engine_.empty()) {
        String ini = joinPath(dirName(legacy_engine_), "cwex.ini");
        if (!exists(ini)) {
            ini = joinPath(dirName(legacy_engine_), "CardWirth.cfg");
            if (!exists(ini)) return result;
        }
        String text;
        try {
            text = readText(ini);
        } catch (const std::invalid_argument& error) {
            (void)utils::printStackTrace();
            utils::debugln(error.what());
            const auto bytes = utils::readBinary(fsPath(ini));
            text = sjis::touni(String(bytes.begin(), bytes.end()));
        }
        for (const auto& line : utils::splitLines(text)) {
            const auto separator = line.find('=');
            if (separator == String::npos || line.find('=', separator + 1U) != String::npos) continue;
            const String key = enumutils::lowerCase(enumutils::stripWhiteSpace(StringView(line).substr(0, separator)));
            const String value = enumutils::stripWhiteSpace(StringView(line).substr(separator + 1));
            result[key] = value;
        }
    }
    if (result.contains("musicvol") && !result.contains("musicapi")) result["musicapi"] = "bass";
    if (result.contains("soundvol") && !result.contains("soundapi")) result["soundapi"] = "bass";
    return result;
}

::cwxeditor::lib::Array<SoundFontWithVolume>
Skin::loadSoundFonts(StringView defaultSoundFont) const {
    if (engine_path_.empty()) return {};
    const String filename = joinPath(dirName(engine_path_), "Settings.xml");
    if (!exists(filename)) return {SoundFontWithVolume(String(defaultSoundFont), 100)};
    bool hasElement = false;
    ::cwxeditor::lib::Array<SoundFontWithVolume> result;
    XNode node = XNode::parse(readText(filename));
    node.onTag["SoundFonts"] = [&hasElement, &result](XNode& fonts) {
        hasElement = true;
        fonts.onTag["SoundFont"] = [&result](XNode& font) {
            if (font.attr<bool>("enabled", false, true) && !font.value().empty()) {
                result.emplace_back(font.value(), font.attr<std::uint32_t>("volume", false, 100));
            }
        };
        fonts.parse();
    };
    node.parse();
    return hasElement ? result : ::cwxeditor::lib::Array<SoundFontWithVolume>{
        SoundFontWithVolume(String(defaultSoundFont), 100)};
}

bool Skin::isMaterial(StringView value, bool check) const {
    return isSE(value, check) || isBGM(value, check) || isBgImage(value, false, check);
}

bool Skin::isSE(StringView value, bool check) const {
    const String ext = extensionLower(value);
    if (legacy_) return ext == ".mp3" || ext == ".ogg" || ext == ".ogv" || ext == ".oga" ||
                            ext == ".ogx" || ext == ".wav";
    (void)check;
    return ext == ".aiff" || ext == ".mid" || ext == ".midi" || ext == ".mod" ||
           ext == ".s3m" || ext == ".xm" || ext == ".it" || ext == ".mt2" || ext == ".669" ||
           ext == ".med" || ext == ".mp3" || ext == ".ogg" || ext == ".ogv" || ext == ".oga" ||
           ext == ".ogx" || ext == ".voc" || ext == ".wav";
}

::cwxeditor::lib::Array<String> Skin::warningSE(const CProps& prop, StringView value,
                                                bool legacy, StringView targetVersion) {
    if (value.empty()) return {};
    const String ext = extensionLower(value);
    ::cwxeditor::lib::Array<String> result;
    if (legacy && (ext == ".ogg" || ext == ".ogv" || ext == ".oga" || ext == ".ogx") &&
        !prop.targetVersion("1.50", targetVersion)) result.push_back(warning(prop, "oggMayNotCorrespond"));
    if (std::find(ext_sound_.begin(), ext_sound_.end(), ext) == ext_sound_.end())
        result.push_back(warning(prop, "warningInvalidFileExtensionSound"));
    return result;
}

bool Skin::isBGM(StringView value, bool check) const {
    const String ext = extensionLower(value);
    if (legacy_) return ext == ".mid" || ext == ".midi" || ext == ".mp3" || ext == ".ogg" ||
                            ext == ".ogv" || ext == ".oga" || ext == ".ogx" || ext == ".wav" || ext == ".mpg";
    (void)check;
    return ext == ".aiff" || ext == ".mid" || ext == ".midi" || ext == ".mod" || ext == ".s3m" ||
           ext == ".xm" || ext == ".it" || ext == ".mt2" || ext == ".669" || ext == ".med" ||
           ext == ".mp3" || ext == ".ogg" || ext == ".ogv" || ext == ".oga" || ext == ".ogx" ||
           ext == ".voc" || ext == ".wav" || ext == ".mpg";
}

::cwxeditor::lib::Array<String> Skin::warningBGM(const CProps& prop, StringView value,
                                                 bool legacy, StringView targetVersion) {
    if (value.empty()) return {};
    const String ext = extensionLower(value);
    ::cwxeditor::lib::Array<String> result;
    if (legacy && ext == ".mp3" && !prop.targetVersion("1.29", targetVersion))
        result.push_back(warning(prop, "mp3LoopMayNotCorrespond"));
    if (legacy && (ext == ".ogg" || ext == ".ogv" || ext == ".oga" || ext == ".ogx") &&
        !prop.targetVersion("1.50", targetVersion)) result.push_back(warning(prop, "oggMayNotCorrespond"));
    if (std::find(ext_bgm_.begin(), ext_bgm_.end(), ext) == ext_bgm_.end())
        result.push_back(warning(prop, "warningInvalidFileExtensionSound"));
    return result;
}

bool Skin::isCardImage(StringView value, bool ignoreSize, bool included) const {
    String ext;
    ::cwxeditor::lib::Bytes bytes;
    if (isBinaryImage(value)) { bytes = utils::strToBImg(value); ext = imagesize::imageType(bytes); }
    else ext = extension(value);
    ext = lower(ext);
    if (legacy_ && ext != ".bmp" && !( !included && (ext == ".png" || ext == ".apng" || ext == ".gif"))) return false;
    if (ignoreSize) return imagesize::isImageExt(value);
    std::uint32_t width = 0, height = 0;
    try {
        if (!bytes.empty()) { if (!imagesize::imageSize(bytes, width, height)) return false; }
        else if (!imagesize::imageSize(fsPath(value), width, height)) return false;
        const auto size = context_->looks.cardSize();
        return width == static_cast<std::uint32_t>(size.width) && height == static_cast<std::uint32_t>(size.height);
    } catch (...) { return false; }
}

bool Skin::isBgImage(StringView value, bool excludeCardSize, bool check) const {
    if (isBinaryImage(value)) return true;
    const String ext = extensionLower(value);
    if (ext == ".jpy1" || ext == ".jptx" || ext == ".jpdc") return true;
    if (legacy_ && ext != ".bmp" && ext != ".jpg" && ext != ".jpeg" && ext != ".png" &&
        ext != ".apng" && ext != ".gif") return false;
    if (excludeCardSize && isCardImage(value, false, false)) return false;
    if (check) { std::uint32_t width = 0, height = 0; try { return imagesize::imageSize(fsPath(value), width, height); } catch (...) { return false; } }
    return imagesize::isImageExt(value);
}

::cwxeditor::lib::Array<String> Skin::warningImage(const CProps& prop, StringView value,
                                                   bool legacy, bool includeType,
                                                   StringView targetVersion) {
    if (value.empty()) return {};
    String ext;
    if (isBinaryImage(value)) { ext = imagesize::imageType(utils::strToBImg(value)); includeType = true; }
    else ext = extensionLower(value);
    ::cwxeditor::lib::Array<String> result;
    if (legacy) {
        if ((ext == ".png" || ext == ".apng") && includeType) result.push_back(warning(prop, "warningIncludedPNGImage"));
        else if ((ext == ".png" || ext == ".apng") && !prop.targetVersion("1.30", targetVersion)) result.push_back(warning(prop, "pngMayNotCorrespond"));
        if (ext == ".gif" && includeType) result.push_back(warning(prop, "warningIncludedGIFImage"));
        else if (ext == ".gif" && !prop.targetVersion("1.30", targetVersion)) result.push_back(warning(prop, "gifMayNotCorrespond"));
    }
    if (!isBinaryImage(value) && std::find(ext_img_.begin(), ext_img_.end(), ext) == ext_img_.end())
        result.push_back(warning(prop, "warningInvalidFileExtensionImage"));
    return result;
}

bool Skin::canInclude(StringView value) const { return !isBinaryImage(value) && extensionLower(value) == ".bmp"; }

::cwxeditor::lib::Array<String> Skin::wsnResDirs(const StringArray& names,
                                                 StringView version) const {
    if (version.empty() || engine_path_.empty() || !exists(engine_path_)) return {};
    const String engineDir = dirName(engine_path_);
    StringArray result;
    for (const auto& candidateVersion : versioninfo::VERSIONS) {
        if (candidateVersion.empty() || !versioninfo::isTargetVersion(version, candidateVersion)) continue;
        for (const auto& name : names) {
            const String directory = joinPath(engineDir, "Data/Materials/Wsn." + candidateVersion + "/" + name);
            if (exists(directory)) result.push_back(directory);
        }
    }
    return result;
}

::cwxeditor::lib::Array<String> Skin::wsnTableDirs(StringView version) const {
    return wsnResDirs({"Table"}, version);
}
::cwxeditor::lib::Array<String> Skin::wsnMusicDirs(StringView version) const {
    return wsnResDirs({"Bgm", "BgmAndSound"}, version);
}
::cwxeditor::lib::Array<String> Skin::wsnSoundDirs(StringView version) const {
    return wsnResDirs({"Sound", "BgmAndSound"}, version);
}

bool Skin::has(StringView dir, bool forceRefresh, int kind, bool flag, bool flag2) const {
    std::scoped_lock cache_lock(resource_cache_mutex_);
    switch (kind) {
    case 0: return hasResource<0>(*this, dir, forceRefresh, flag, flag2);
    case 1: return hasResource<1>(*this, dir, forceRefresh, flag, flag2);
    case 2: return hasResource<2>(*this, dir, forceRefresh, flag, flag2);
    case 3: return hasResource<3>(*this, dir, forceRefresh, flag, flag2);
    default: throw std::logic_error("Unknown resource predicate");
    }
}

bool Skin::hasCardImage(StringView dir, bool forceRefresh, bool ignoreSize, bool included) const {
    return has(dir, forceRefresh, 0, ignoreSize, included);
}
bool Skin::hasBgImage(StringView dir, bool forceRefresh, bool excludeCardSize) const {
    return has(dir, forceRefresh, 1, excludeCardSize, false);
}
bool Skin::hasBGM(StringView dir, bool forceRefresh) const { return has(dir, forceRefresh, 2, false, false); }
bool Skin::hasSE(StringView dir, bool forceRefresh) const { return has(dir, forceRefresh, 3, false, false); }
bool Skin::hasWsnCardImage(StringView version, bool forceRefresh, bool ignoreSize) const {
    for (const auto& dir : wsnTableDirs(version)) if (hasCardImage(dir, forceRefresh, ignoreSize, false)) return true;
    return false;
}
bool Skin::hasWsnBgImage(StringView version, bool forceRefresh, bool excludeCardSize) const {
    for (const auto& dir : wsnTableDirs(version)) if (hasBgImage(dir, forceRefresh, excludeCardSize)) return true;
    return false;
}
bool Skin::hasWsnBGM(StringView version, bool forceRefresh) const {
    for (const auto& dir : wsnMusicDirs(version)) if (hasBGM(dir, forceRefresh)) return true;
    return false;
}
bool Skin::hasWsnSE(StringView version, bool forceRefresh) const {
    for (const auto& dir : wsnSoundDirs(version)) if (hasSE(dir, forceRefresh)) return true;
    return false;
}

::cwxeditor::lib::SharedSlice<String> Skin::listImpl(StringView dir, bool forceRefresh,
                                                    int kind, bool flag, bool flag2) const {
    std::scoped_lock cache_lock(resource_cache_mutex_);
    switch (kind) {
    case 0: return listResourceFiles<0>(*this, dir, forceRefresh, flag, flag2);
    case 1: return listResourceFiles<1>(*this, dir, forceRefresh, flag, flag2);
    case 2: return listResourceFiles<2>(*this, dir, forceRefresh, flag, flag2);
    case 3: return listResourceFiles<3>(*this, dir, forceRefresh, flag, flag2);
    default: throw std::logic_error("Unknown resource predicate");
    }
}

StringArray Skin::list(const StringArray& dirs, bool logicalSort, bool forceRefresh,
                       int kind, bool flag, bool flag2, bool sortResult) const {
    StringArray result;
    for (const auto& dir : dirs) {
        auto values = listImpl(dir, forceRefresh, kind, flag, flag2);
        if (!values.empty()) result.insert(result.end(), values.begin(), values.end());
    }
    if (sortResult) {
        if (logicalSort) std::sort(result.begin(), result.end(), [](const String& a, const String& b) { return utils::fnncmp(a, b) < 0; });
        else std::sort(result.begin(), result.end(), [](const String& a, const String& b) { return utils::fncmp(a, b) < 0; });
    }
    return result;
}

StringArray Skin::cards(StringView dir, bool logicalSort, bool forceRefresh,
                        bool ignoreSize, bool included) const {
    return list({String(dir)}, logicalSort, forceRefresh, 0, ignoreSize, included);
}
StringArray Skin::tables(bool logicalSort, bool forceRefresh, bool excludeCardSize) const {
    return list(tableDirs(), logicalSort, forceRefresh, 1, excludeCardSize, false);
}
StringArray Skin::tables(StringView dir, bool logicalSort, bool forceRefresh, bool excludeCardSize) const {
    return list({String(dir)}, logicalSort, forceRefresh, 1, excludeCardSize, false);
}
StringArray Skin::musics(bool logicalSort, bool forceRefresh) const {
    return list(bgmDirs(), logicalSort, forceRefresh, 2, false, false);
}
StringArray Skin::musics(StringView dir, bool logicalSort, bool forceRefresh) const {
    return list({String(dir)}, logicalSort, forceRefresh, 2, false, false);
}
StringArray Skin::sounds(bool logicalSort, bool forceRefresh) const {
    return list(seDirs(), logicalSort, forceRefresh, 3, false, false);
}
StringArray Skin::sounds(StringView dir, bool logicalSort, bool forceRefresh) const {
    return list({String(dir)}, logicalSort, forceRefresh, 3, false, false);
}
StringArray Skin::wsnCards(StringView version, bool logicalSort, bool forceRefresh, bool ignoreSize) const {
    return list(wsnTableDirs(version), logicalSort, forceRefresh, 0, ignoreSize, false);
}
StringArray Skin::wsnTables(StringView version, bool logicalSort, bool forceRefresh, bool excludeCardSize) const {
    return list(wsnTableDirs(version), logicalSort, forceRefresh, 1, excludeCardSize, false);
}
StringArray Skin::wsnMusics(StringView version, bool logicalSort, bool forceRefresh) const {
    return list(wsnMusicDirs(version), logicalSort, forceRefresh, 2, false, false);
}
StringArray Skin::wsnSounds(StringView version, bool logicalSort, bool forceRefresh) const {
    return list(wsnSoundDirs(version), logicalSort, forceRefresh, 3, false, false);
}

String Skin::resDir() const { return legacy_path_.empty() ? path_ : legacy_path_; }
StringArray Skin::tableDirs() const { return {joinPath(resDir(), "Table")}; }
StringArray Skin::bgmDirs() const {
    return legacy_path_.empty() ? StringArray{joinPath(path_, "Bgm"), joinPath(path_, "BgmAndSound")}
                                 : StringArray{joinPath(legacy_path_, "Midi")};
}
StringArray Skin::seDirs() const {
    return legacy_path_.empty() ? StringArray{joinPath(path_, "Sound"), joinPath(path_, "BgmAndSound")}
                                 : StringArray{joinPath(legacy_path_, "Wave")};
}
String Skin::resourceDir() const { return joinPath(joinPath(path_, "Resource"), "Image"); }
const StringArray& Skin::extImage() const noexcept { return ext_img_; }
const StringArray& Skin::extBgm() const noexcept { return ext_bgm_; }
const StringArray& Skin::extSound() const noexcept { return ext_sound_; }

String Skin::defBattle(StringView scenarioPath, StringView wsnVersion) const {
    const auto defaults = bgmDirs();
    const auto wsn = wsnMusicDirs(wsnVersion);
    return baseName(findPath("DefBattle.mid", ext_bgm_, defaults, scenarioPath,
                             wsnVersion, wsn));
}

String Skin::findImagePathF(StringView path, StringView scenarioPath, StringView wsnVersion,
                            bool& isSkinMaterial, bool& isEngineMaterial) const {
    isSkinMaterial = false;
    isEngineMaterial = false;
    const auto defaults = tableDirs();
    const auto wsn = wsnTableDirs(wsnVersion);
    return findPathF(path, ext_img_, defaults, scenarioPath, wsnVersion,
                     wsn, isSkinMaterial, isEngineMaterial);
}
String Skin::findImagePath(StringView path, StringView scenarioPath, StringView wsnVersion) const {
    bool dummy = false;
    return findImagePathF(path, scenarioPath, wsnVersion, dummy, dummy);
}
String Skin::findImagePath(StringView path, StringView scenarioPath, StringView wsnVersion,
                           std::size_t& defaultIndex) const {
    defaultIndex = 0;
    bool dummy = false;
    const auto defaults = tableDirs();
    const auto wsn = wsnTableDirs(wsnVersion);
    return findPathF(path, ext_img_, defaults, scenarioPath, wsnVersion,
                     wsn, dummy, dummy, defaultIndex);
}

String Skin::findPathF(StringView value, const StringArray& extensions,
                       const StringArray& defaultDirs, StringView scenarioPath,
                       StringView wsnVersion, const StringArray& wsnDirs,
                       bool& isSkinMaterial, bool& isEngineMaterial) const {
    isSkinMaterial = false;
    isEngineMaterial = false;
    std::size_t index = 0;
    return findPathF(value, extensions, defaultDirs, scenarioPath, wsnVersion,
                    wsnDirs, isSkinMaterial, isEngineMaterial, index);
}

String Skin::findPathF(StringView value, const StringArray& extensions,
                       const StringArray& defaultDirs, StringView scenarioPath,
                       StringView wsnVersion, const StringArray& wsnDirs,
                       bool& isSkinMaterial, bool& isEngineMaterial,
                       std::size_t& defaultIndex) const {
    isSkinMaterial = false; isEngineMaterial = false; defaultIndex = 0;
    if (value.empty()) return {};
    if (isBinaryImage(value)) return String(value);
    String candidate;
    if (!scenarioPath.empty()) {
        candidate = joinPath(scenarioPath, value);
        if (exists(candidate)) return candidate;
    }
    isSkinMaterial = true;
    for (std::size_t index = 0; index < defaultDirs.size(); ++index) {
        const auto& directory = defaultDirs[index];
        candidate = joinPath(directory, baseName(value));
        if (exists(candidate)) { defaultIndex = index; return candidate; }
        for (const auto& ext : extensions) {
            candidate = setExtension(candidate, ext);
            if (exists(candidate)) { defaultIndex = index; return candidate; }
        }
    }
    isSkinMaterial = false;
    if (!legacy_ && !engine_path_.empty() && exists(engine_path_) && !wsnVersion.empty()) {
        isEngineMaterial = true;
        // Dは既定ディレクトリごとにWSN候補を繰り返す。その一覧が空
        // なら検索しない動作も含める。使われないdirnameスライス
        // のために、ネイティブの文字列やパスを追加確保する必要はない。
        for (const auto& defaultDirectory : defaultDirs) {
            (void)defaultDirectory;
            for (const auto& directory : wsnDirs) {
                candidate = joinPath(directory, baseName(value));
                if (exists(candidate)) return candidate;
                for (const auto& ext : extensions) {
                    candidate = setExtension(candidate, ext);
                    if (exists(candidate)) return candidate;
                }
            }
        }
        isEngineMaterial = false;
    }
    return {};
}

String Skin::findPath(StringView value, const StringArray& extensions,
                      const StringArray& defaultDirs, StringView scenarioPath,
                      StringView wsnVersion, const StringArray& wsnDirs) const {
    bool dummy = false;
    return findPathF(value, extensions, defaultDirs, scenarioPath, wsnVersion, wsnDirs, dummy, dummy);
}
bool Skin::isSkinResource(StringView value, const StringArray& extensions,
                          const StringArray& defaultDirs, StringView scenarioPath) const {
    bool skin = false, engine = false;
    (void)findPathF(value, extensions, defaultDirs, scenarioPath, {}, {}, skin, engine);
    return skin;
}

String Skin::evtChildOK() const {
    return classic_engine_.okText.has_value() ? *classic_engine_.okText
                                               : context_->system->evtChildOK(legacyName());
}

::cwxeditor::lib::Array<Sex> Skin::allSexes() const {
    ::cwxeditor::lib::Array<Sex> result;
    const std::size_t count = sexes_.empty() ? 2 : sexes_.size();
    for (std::size_t i = 0; i < count; ++i) result.emplace_back(i);
    return result;
}

::cwxeditor::lib::Array<Period> Skin::allPeriods() const {
    ::cwxeditor::lib::Array<Period> result;
    const std::size_t count = periods_.empty() ? 4 : periods_.size();
    for (std::size_t i = 0; i < count; ++i) result.emplace_back(i);
    return result;
}

::cwxeditor::lib::Array<Nature> Skin::allNatures() const {
    ::cwxeditor::lib::Array<Nature> result;
    const std::size_t count = natures_.empty() ? 12 : natures_.size();
    for (std::size_t i = 0; i < count; ++i) result.emplace_back(i);
    return result;
}

::cwxeditor::lib::Array<Nature> Skin::normalNatures() const {
    return selectNatures(natures_, false);
}

::cwxeditor::lib::Array<Nature> Skin::extraNatures() const {
    return selectNatures(natures_, true);
}

::cwxeditor::lib::Array<Makings> Skin::allMakings() const {
    ::cwxeditor::lib::Array<Makings> result;
    const std::size_t count = makings_.empty() ? 48 : makings_.size();
    for (std::size_t i = 0; i < count; ++i) result.emplace_back(i);
    return result;
}
::cwxeditor::lib::Array<Makings> Skin::leftMakings() const {
    ::cwxeditor::lib::Array<Makings> result;
    const std::size_t count = makings_.empty() ? 48 : makings_.size();
    for (std::size_t i = 0; i < count; i += 2) result.emplace_back(i);
    return result;
}
::cwxeditor::lib::Array<Makings> Skin::rightMakings() const {
    ::cwxeditor::lib::Array<Makings> result;
    const std::size_t count = makings_.empty() ? 48 : makings_.size();
    for (std::size_t i = 1; i < count; i += 2) result.emplace_back(i);
    return result;
}
Makings Skin::reverseMakings(Makings value) const {
    return Makings(value.value & 1U ? value.value - 1U : value.value + 1U);
}

String Skin::sexName(Sex value) const {
    if (const auto found = sexes_.find(value); found != sexes_.end() && found->second) return found->second->name();
    const String key = context_->system->sexName(value, {});
    if (const auto found = classic_engine_.sexName.find(key); found != classic_engine_.sexName.end()) return found->second;
    return context_->system->sexName(value, legacyName());
}
String Skin::periodName(Period value) const {
    if (const auto found = periods_.find(value); found != periods_.end() && found->second) return found->second->name();
    const String key = context_->system->periodName(value, {});
    if (const auto found = classic_engine_.periodName.find(key); found != classic_engine_.periodName.end()) return found->second;
    return context_->system->periodName(value, legacyName());
}
String Skin::natureName(Nature value) const {
    if (const auto found = natures_.find(value); found != natures_.end() && found->second) return found->second->name();
    const String key = context_->system->natureName(value, {});
    if (const auto found = classic_engine_.natureName.find(key); found != classic_engine_.natureName.end()) return found->second;
    return context_->system->natureName(value, legacyName());
}
String Skin::makingsName(Makings value) const {
    if (const auto found = makings_.find(value); found != makings_.end() && found->second) return found->second->name();
    const String key = context_->system->makingsName(value, {});
    if (const auto found = classic_engine_.makingsName.find(key); found != classic_engine_.makingsName.end()) return found->second;
    return context_->system->makingsName(value, legacyName());
}

String Skin::sexCoupon(Sex value) const { return context_->system->convCoupon(sexName(value), CouponType::Hide, false); }
String Skin::periodCoupon(Period value) const { return context_->system->convCoupon(periodName(value), CouponType::Hide, false); }
String Skin::natureCoupon(Nature value) const { return context_->system->convCoupon(natureName(value), CouponType::Hide, false); }
String Skin::makingsCoupon(Makings value) const { return context_->system->convCoupon(makingsName(value), CouponType::Hide, false); }

::cwxeditor::lib::Array<::cwxeditor::lib::Owner<Coupon>>
Skin::periodInitialCoupons(const System& system, Period value) const {
    if (!legacy_) {
        if (const auto found = periods_.find(value); found != periods_.end() && found->second)
            return found->second->coupons();
        return {};
    }
    return system.periodInitialCoupons(value, legacyName());
}
String Skin::number1Coupon(const System& system) const {
    return (!legacy_ && !number1_coupon_.empty()) ? number1_coupon_ : system.number1Coupon(legacyName());
}
String Skin::replaceSystemCouponName(const System& system, StringView coupon) const {
    return coupon == system.number1Coupon({}) ? number1Coupon(system) : String(coupon);
}

::cwxeditor::lib::Owner<::cwxeditor::cwx::card::ActionCard>
Skin::actionCard(ActionCardType type) const {
    const auto found = action_cards_.find(type);
    return found == action_cards_.end() ? nullptr : found->second;
}
String Skin::actionCardName(const System& system, ActionCardType type) const {
    if (const auto card = actionCard(type)) return detail::actionCardName(*card, system);
    if (const auto found = classic_engine_.actionCardName.find(type); found != classic_engine_.actionCardName.end()) return found->second;
    return system.actionCardName(type, legacyName());
}
Aptitude Skin::actionCardAptitude(const System& system, ActionCardType type) const {
    if (const auto card = actionCard(type)) return detail::actionCardAptitude(*card);
    return system.actionCardAptitude(type);
}

::cwxeditor::lib::Array<ActionCardType> Skin::actionCardTypes() const {
    ::cwxeditor::lib::Array<ActionCardType> values;
    if (legacy_ || action_cards_.empty()) {
        values = {ActionCardType::Exchange, ActionCardType::Attack, ActionCardType::PowerfulAttack,
                  ActionCardType::CriticalAttack, ActionCardType::Feint, ActionCardType::Defense,
                  ActionCardType::Distance, ActionCardType::Confuse, ActionCardType::RunAway};
    } else {
        for (const auto& [type, card] : action_cards_) if (card) values.push_back(type);
        std::sort(values.begin(), values.end(), [](ActionCardType a, ActionCardType b) {
            return static_cast<std::int32_t>(a) < static_cast<std::int32_t>(b);
        });
    }
    ::cwxeditor::lib::Array<ActionCardType> result;
    for (const auto type : values) if (type != ActionCardType::RunAway && static_cast<std::int32_t>(type) >= 0) result.push_back(type);
    for (const auto type : values) if (static_cast<std::int32_t>(type) < 0) result.push_back(type);
    for (const auto type : values) if (type == ActionCardType::RunAway) result.push_back(type);
    return result;
}

void Skin::resetLoadedData() {
    races_.clear(); sexes_.clear(); periods_.clear(); natures_.clear(); makings_.clear();
    number1_coupon_.clear(); action_cards_.clear(); sp_chars_.clear();
    source_of_materials_is_classic_engine_ = false;
}

void Skin::initialize() {
    std::scoped_lock initialization_lock(initialization_mutex_);
    if (!version_) return;
    const String fontDirectory = utils::buildPath(resourceDir(), "Font");
    for (const auto& entry : utils::clistdir(fsPath(fontDirectory))) {
        // DのnoScaledPathは、x2/x4形式の拡大
        // 画像の場合だけ空でないパスを返す。通常画像は空パスを
        // 返すが、特殊文字のグリフとして登録する必要がある。
        if (!utils::noScaledPath(fsPath(entry)).empty()) continue;
        const String file = utils::buildPath(fontDirectory, entry);
        if (isDirectory(file) || !imagesize::isImageExt(file)) continue;
        const String stem = stripExtension(baseName(file));
        const auto points = enumutils::toUtf32Replacing(stem);
        char32_t key = upperCodepoint(points.at(0));
        switch (key) {
        case U'M': case U'R': case U'U': case U'C': case U'I': case U'T': case U'Y':
            key = upperCodepoint(points.back()); break;
        default: break;
        }
        sp_chars_[key] = file;
    }
    const String actionCardDirectory = utils::buildPath(
        utils::buildPath(utils::buildPath(path_, "Resource"), "Xml"), "ActionCard");
    for (utils::ShallowDirectory entries(fsPath(actionCardDirectory)); !entries.empty(); entries.popFront()) {
        auto entry = entries.front();
        const auto& file = entry.name();
        if (!entry.isFile()) return;
        if (enumutils::lowerCase(utils::pathExtension(file)) != ".xml") continue;
        try {
            XNode node = XNode::parse(readText(file));
            if (node.name() != "ActionCard") return;
            const auto version = version_;
            auto card = detail::actionCardFromNode(node, requiredXMLInfo(version));
            if (!card) throw std::logic_error("Missing ActionCard result");
            action_cards_[detail::actionCardType(*card)] = std::move(card);
        } catch (const std::bad_alloc&) {
            // Dがここで捕捉するのはExceptionであり、メモリ確保のErrorではない。
            throw;
        } catch (const std::exception& error) {
            (void)utils::printStackTrace();
            utils::debugln(baseName(file));
            utils::debugln(error.what());
        }
    }
    version_.reset();
}

void Skin::loadFromXML(const CProps& prop, StringView filename, const XMLInfo& version) {
    loadFromXML(prop, filename, std::make_shared<const XMLInfo>(version));
}

void Skin::loadFromXML(const CProps& prop, StringView filename,
                        ::cwxeditor::lib::Owner<const XMLInfo> version) {
    // Dは不変のファイル名値を保持する。ネイティブビューはXMLコールバッ
    // クで置換される項目を参照し得るため、診断用にもバイト列を保持する。
    const String filename_storage(filename);
    filename = filename_storage;
    try {
        version_ = version; path_ = dirName(filename); skin_file_ = filename;
        XNode root = XNode::parse(readText(filename));
        resetLoadedData();
        root.onTag["Property"] = [this](XNode& node) {
            node.onTag["Name"] = [this](XNode& child) { name_ = child.value(); };
            node.onTag["Type"] = [this](XNode& child) { type_ = child.value(); };
            node.onTag["Author"] = [this](XNode& child) { author_ = child.value(); };
            node.onTag["Description"] = [this](XNode& child) { desc_ = child.value(); };
            node.onTag["SourceOfMaterialsIsClassicEngine"] = [this](XNode& child) {
                source_of_materials_is_classic_engine_ = utils::parseBool(child.value());
            };
            node.parse();
        };
        root.onTag["Races"] = [this, &prop, version](XNode& node) {
            node.onTag["Race"] = [this, &prop, version](XNode& child) {
                races_.push_back(race::Race::fromNode(prop, child, requiredXMLInfo(version)));
            };
            node.parse();
        };
        root.onTag["Sexes"] = [this, version](XNode& node) {
            node.onTag["Sex"] = [this, version](XNode& child) {
                sexes_[Sex(sexes_.size())] = ModData::fromNode(child, requiredXMLInfo(version));
            };
            node.parse();
        };
        root.onTag["Periods"] = [this, version](XNode& node) {
            node.onTag["Period"] = [this, version](XNode& child) {
                periods_[Period(periods_.size())] = ModData::fromNode(child, requiredXMLInfo(version));
            };
            node.parse();
        };
        root.onTag["Natures"] = [this, version](XNode& node) {
            node.onTag["Nature"] = [this, version](XNode& child) {
                natures_[Nature(natures_.size())] = ModData::fromNode(child, requiredXMLInfo(version));
            };
            node.parse();
        };
        root.onTag["Makings"] = [this, version](XNode& node) {
            node.onTag["Making"] = [this, version](XNode& child) {
                makings_[Makings(makings_.size())] = ModData::fromNode(child, requiredXMLInfo(version));
            };
            node.parse();
        };
        root.onTag["Messages"] = [this](XNode& node) {
            node.onTag["Message"] = [this](XNode& child) {
                if (child.attr<String>("key", false, {}) == "number_1_coupon") number1_coupon_ = child.value();
            };
            node.parse();
        };
        root.parse();
    } catch (...) {
        (void)utils::printStackTrace();
        utils::debugln(filename);
        try { throw; }
        catch (const std::exception& error) { utils::debugln(error.what()); }
        catch (...) { utils::debugln("Unknown native Skin.xml failure"); }
        throw std::runtime_error("Invalid Skin.xml.");
    }
}

::cwxeditor::lib::Owner<ModData> ModData::fromNode(XNode& node, const XMLInfo& version) {
    auto result = std::make_shared<ModData>();
    result->special_ = node.attr<bool>("special", false, false);
    node.onTag["Name"] = [result](XNode& child) { result->name_ = child.value(); };
    node.onTag["Physical"] = [result](XNode& child) {
        result->physical_[Physical::Agl] = child.attr<std::int32_t>("agl", false, 0);
        result->physical_[Physical::Dex] = child.attr<std::int32_t>("dex", false, 0);
        result->physical_[Physical::Int] = child.attr<std::int32_t>("int", false, 0);
        result->physical_[Physical::Min] = child.attr<std::int32_t>("min", false, 0);
        result->physical_[Physical::Str] = child.attr<std::int32_t>("str", false, 0);
        result->physical_[Physical::Vit] = child.attr<std::int32_t>("vit", false, 0);
    };
    node.onTag["Mental"] = [result](XNode& child) {
        result->mental_[Mental::Aggressive] = child.attr<double>("aggressive", false, 0.0);
        result->mental_[Mental::Brave] = child.attr<double>("brave", false, 0.0);
        result->mental_[Mental::Cautious] = child.attr<double>("cautious", false, 0.0);
        result->mental_[Mental::Cheerful] = child.attr<double>("cheerful", false, 0.0);
        result->mental_[Mental::Trickish] = child.attr<double>("trickish", false, 0.0);
    };
    node.onTag[String(Coupon::XML_NAME_M)] = [result, &version](XNode& child) {
        child.onTag[String(Coupon::XML_NAME)] = [result, &version](XNode& coupon) {
            result->coupons_.push_back(Coupon::fromNode(coupon, version));
        };
        child.parse();
    };
    node.parse();
    return result;
}

}  // namespace cwxeditor::cwx::skin
