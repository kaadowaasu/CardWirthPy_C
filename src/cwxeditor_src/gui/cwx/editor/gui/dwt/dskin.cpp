#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dskin.hpp"
#include "cwxeditor_src/core/cwx/perf.hpp"
#include "cwxeditor_src/lib/image_mask.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::dskin {
namespace {

using Path = std::filesystem::path;
using Services = lib::Services;
using ::cwxeditor::cwx::structs::pathFromUtf8;
using ::cwxeditor::cwx::structs::pathToUtf8;

Path absolutePath(const Path& value) {
    if (value.empty()) return {};
    std::error_code error;
    const Path absolute = value.is_absolute()
        ? value : std::filesystem::absolute(value, error);
    return (error ? value : absolute).lexically_normal();
}

bool regularFile(const Path& value) {
    std::error_code error;
    return std::filesystem::is_regular_file(value, error) && !error;
}

std::uint32_t positiveScale(const std::uint32_t value) noexcept {
    return std::max<std::uint32_t>(1U, value);
}

struct CachedImage {
    ImageDataWithScale image;
    std::weak_ptr<lib::ImageCodecBackend> codec;
    std::size_t bytes{};
    std::uint64_t order{};
};
std::unordered_map<String,CachedImage>& imageCache() {
    static std::unordered_map<String,CachedImage> cache;return cache;
}
std::mutex& imageCacheMutex(){static std::mutex mutex;return mutex;}
std::optional<ImageDataWithScale> cachedImage(const String& key,const Services& services) {
    std::scoped_lock lock(imageCacheMutex());
    const auto found=imageCache().find(key);
    if(found!=imageCache().end() && found->second.codec.lock()==services.image_codecs)
        return found->second.image;
    return {};
}
void cacheImage(String key,const ImageDataWithScale& image,const Services& services) {
    // DのFileCacheに合わせ、1024項目とバイト容量の上限を使う。共通
    // コーデックの同一性、マスク、倍率もキーに含め、別バックエンドの画素混在と
    // Dのマスクキャッシュの衝突を防ぐ。失効したバックエンドの項目は破棄する。
    constexpr std::uint64_t budget=sizeof(void*)>=8?32ULL*1024*1024*1024:512ULL*1024*1024;
    std::size_t bytes{};
    for(const auto& frame:image.data.frames) {
        if(frame.pixels.size()>budget-bytes)return;
        bytes+=frame.pixels.size();
    }
    std::scoped_lock lock(imageCacheMutex());
    auto& cache=imageCache();
    std::erase_if(cache,[](const auto& item){return item.second.codec.expired();});
    cache.erase(key);
    std::uint64_t total{};for(const auto& [name,item]:cache){(void)name;total+=item.bytes;}
    while(!cache.empty() && (cache.size()>=1024 || total+bytes>budget)) {
        const auto oldest=std::min_element(cache.begin(),cache.end(),[](const auto& a,const auto& b){return a.second.order<b.second.order;});
        total-=oldest->second.bytes;cache.erase(oldest);
    }
    static std::uint64_t next{};
    cache.emplace(std::move(key),CachedImage{image,services.image_codecs,bytes,++next});
}

String imageCacheKey(const Path& path, const std::uint32_t scale,
                     const MaskType mask) {
    String key = pathToUtf8(absolutePath(path));
    key.push_back('\n');
    key += std::to_string(scale);
    key.push_back('\n');
    key += std::to_string(static_cast<int>(mask));
    std::error_code error;
    if (regularFile(path)) {
        key.push_back('\n');
        key += std::to_string(std::filesystem::file_size(path, error));
        error.clear();
        const auto stamp = std::filesystem::last_write_time(path, error);
        if (!error) {
            key.push_back('\n');
            key += std::to_string(static_cast<long long>(
                stamp.time_since_epoch().count()));
        }
    }
    return key;
}

Services platformServices(Services services = {}) {
    if (!services.graphics && !services.image_codecs) {
        services = lib::make_platform_services();
    }
    return services;
}

void applyMask(ImageDataWithScale& image, const MaskType maskType) {
    if (maskType == MaskType::NoMask) return;
    for (auto& frame : image.data.frames) {
        if (frame.pixel_width == 0 || frame.pixel_height == 0) continue;
        std::int64_t x = 0, y = 0;
        if (maskType == MaskType::RightMask) x = frame.pixel_width - 1U;
        else if (maskType == MaskType::Mask1_1) {
            // Dのimgdはimgr経路の寸法検査なしで(1,1)
            // を読む。安全性の修正として、いずれかの寸法が1なら(1,1)
            // 画素は存在しないため、マスクせず画像を残す。
            if (frame.pixel_width <= 1U || frame.pixel_height <= 1U) continue;
            x = y = 1;
        }
        lib::apply_image_color_key(frame, x, y, true);
    }
}

ImageDataWithScale decodePath(const Path& requestedPath,
                              const std::uint32_t requestedScale,
                              const MaskType maskType,
                              Services services) {
    const auto scale = positiveScale(requestedScale);
    if (requestedPath.empty()) return {};

    const auto scaled = ::cwxeditor::cwx::utils::findScaledImage(requestedPath, scale);
    const Path source = regularFile(scaled.path) ? scaled.path : requestedPath;
    services=platformServices(std::move(services));
    const auto key=imageCacheKey(source,scale,maskType)+"\n"+std::to_string(reinterpret_cast<std::uintptr_t>(services.image_codecs.get()));
    if(auto cached=cachedImage(key,services))return *cached;
    if (!services.image_codecs || !regularFile(source)) return {};

    lib::DecodeImageRequest request;
    request.path = source;
    request.format_hint = lib::ImageFormat::Unknown;
    std::string error;
    lib::ImageData decoded;
    try {
        decoded = services.image_codecs->decode(request, error);
    } catch (...) {
        return {};
    }
    if (decoded.frames.empty()) return {};

    ImageDataWithScale result(std::move(decoded),
                              positiveScale(scaled.scale == 0U ? scale : scaled.scale));
    applyMask(result, maskType);
    cacheImage(key,result,services);
    return result;
}

ImageDataWithScale decodeBinary(const StringView value,
                                const std::uint32_t requestedScale,
                                const MaskType maskType,
                                Services services) {
    const auto scale = positiveScale(requestedScale);
    if (!::cwxeditor::cwx::utils::isBinImg(value)) return {};
    services=platformServices(std::move(services));
    const auto key=String(value)+'\n'+std::to_string(scale)+'\n'+
        std::to_string(static_cast<int>(maskType))+'\n'+std::to_string(reinterpret_cast<std::uintptr_t>(services.image_codecs.get()));
    if(auto cached=cachedImage(key,services))return *cached;
    if (!services.image_codecs) return {};
    lib::Bytes encoded;
    try {
        encoded = ::cwxeditor::cwx::utils::strToBImg(value);
    } catch (...) {
        return {};
    }
    lib::DecodeImageRequest request;
    request.encoded_data = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(encoded.data()), encoded.size());
    request.format_hint = lib::ImageFormat::Unknown;
    std::string error;
    lib::ImageData decoded;
    try {
        decoded = services.image_codecs->decode(request, error);
    } catch (...) {
        return {};
    }
    if (decoded.frames.empty()) return {};
    ImageDataWithScale result(std::move(decoded), 1U);
    applyMask(result, maskType);
    cacheImage(key,result,services);
    return result;
}

ImageDataWithScale decodeString(const StringView path,
                                const std::uint32_t targetScale,
                                const MaskType maskType,
                                Services services = {}) {
    if (::cwxeditor::cwx::utils::isBinImg(path)) {
        return decodeBinary(path, targetScale, maskType, std::move(services));
    }
    return decodePath(pathFromUtf8(path), targetScale, maskType,
                      std::move(services));
}

ImageDataWithScale resourceImage(const StringView legacyEngine,
                                 const StringView resourceName,
                                 const std::uint32_t targetScale,
                                 const MaskType maskType,
                                 Services services = {}) {
    if (legacyEngine.empty() || resourceName.empty()) return {};
    Path engine = pathFromUtf8(legacyEngine);
    Path resource = pathFromUtf8(resourceName);
    resource.replace_extension(pathFromUtf8(".bmp"));
    const Path overridePath = engine.parent_path() / pathFromUtf8("Data") /
                              pathFromUtf8("Resource") / resource;
    if (regularFile(overridePath)) {
        ::cwxeditor::cwx::perf::TraceScope trace_dskin_d219(__FILE__, __LINE__);
        return decodePath(overridePath, targetScale, maskType,
                          std::move(services));
    }
    services = platformServices(std::move(services));
    if (!services.system || !services.image_codecs) return {};
    std::string error;
    auto dib = services.system->executable_resource(engine, "BITMAP", resourceName, error);
    // ネイティブLoadBitmapの代わりに、一つの共通のファイル形式への包装を使
    // う。DIB COREのパレットは3バイト。40バイトヘッダー外のINFOビット
    // フィールドマスクはパレットより前に置く。資源の全バイトはリトルエンディアン。
    if(dib.size()<12U || dib.size()>std::numeric_limits<std::uint32_t>::max()-14U)return {};
    const auto read16=[&](std::size_t offset) {
        return std::uint16_t(std::to_integer<unsigned>(dib[offset]) |
               (std::to_integer<unsigned>(dib[offset+1])<<8U));
    };
    const auto read32=[&](std::size_t offset) {
        return std::uint32_t(read16(offset)) | (std::uint32_t(read16(offset+2))<<16U);
    };
    const auto header=read32(0);
    if(header>dib.size() || (header!=12U && header<40U))return {};
    const bool core=header==12U;
    const auto bits=read16(core?10U:14U);
    const auto planes=read16(core?8U:12U);
    if(planes!=1U || (bits!=1U && bits!=4U && bits!=8U && bits!=16U && bits!=24U && bits!=32U))return {};
    const auto compression=core?0U:read32(16);
    const auto used=core?0U:read32(32);
    const auto entries=used?used:bits<=8U?(1U<<bits):0U;
    if(bits<=8U && entries>(1U<<bits))return {};
    const std::size_t masks=header==40U?(compression==3U?12U:compression==6U?16U:0U):0U;
    const std::size_t paletteWidth=core?3U:4U;
    if(masks>dib.size()-header || entries>(dib.size()-header-masks)/paletteWidth)return {};
    const auto offset=14U+std::size_t(header)+masks+std::size_t(entries)*paletteWidth;
    std::vector<std::byte> bitmap(14U+dib.size());
    bitmap[0]=std::byte{'B'};bitmap[1]=std::byte{'M'};
    const auto write32=[&](std::size_t position,std::uint32_t value) {
        for(unsigned i=0;i<4;++i)bitmap[position+i]=std::byte((value>>(8*i))&255U);
    };
    write32(2,static_cast<std::uint32_t>(bitmap.size()));
    write32(10,static_cast<std::uint32_t>(offset));
    std::copy(dib.begin(),dib.end(),bitmap.begin()+14);
    lib::DecodeImageRequest request;
    request.encoded_data = std::span<const std::byte>(bitmap.data(), bitmap.size());
    request.format_hint = lib::ImageFormat::Bitmap;
    auto decoded = services.image_codecs->decode(request, error);
    if (decoded.frames.empty()) return {};
    ImageDataWithScale result(std::move(decoded), 1U);
    applyMask(result, maskType);
    (void)targetScale;
    return result;
}

Path resolveExecutable(StringView command, Services services = {}) {
    std::string value(command);
    services = platformServices(std::move(services));
    if (services.shell && value.find_first_of(" \t\r\n\"") == std::string::npos) {
        const auto requested = pathFromUtf8(value);
        auto extension = requested.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (!extension.empty() && extension != ".exe" && extension != ".com" &&
            extension != ".bat" && extension != ".cmd") {
            const auto associated = services.shell->associated_command(requested);
            if (!associated.empty()) value = associated;
        }
    }
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    value.erase(0, first);
    if (value.front() == '\"') {
        const auto end = value.find('\"', 1);
        value = value.substr(1, end == std::string::npos ? std::string::npos : end - 1);
    } else {
        const auto end = value.find_first_of(" \t\r\n");
        if (end != std::string::npos) value.resize(end);
    }
    if (value.empty()) return {};

    Path candidate = pathFromUtf8(value);
    if (candidate.is_absolute() || regularFile(candidate)) return absolutePath(candidate);

    const auto service_path = services.system ? services.system->environment("PATH") : std::nullopt;
    const char* fallback_path = std::getenv("PATH");
    const std::string path_value = service_path.value_or(fallback_path ? fallback_path : "");
    if (!path_value.empty()) {
        const bool windows = services.system &&
                             services.system->platform() == lib::Platform::Windows;
        const char separator = windows ? ';' : ':';
        const std::string& paths = path_value;
        std::size_t begin = 0;
        while (begin <= paths.size()) {
            const auto end = paths.find(separator, begin);
            const auto directory = paths.substr(begin,
                end == std::string::npos ? std::string::npos : end - begin);
            if (!directory.empty()) {
                Path found = pathFromUtf8(directory) / candidate;
                if (regularFile(found)) return absolutePath(found);
                if (windows && !candidate.has_extension()) {
                    found += ".exe";
                    if (regularFile(found)) return absolutePath(found);
                }
            }
            if (end == std::string::npos) break;
            begin = end + 1U;
        }
    }
    return absolutePath(candidate);
}

Path resolveExistingExecutable(StringView command, Services services = {}) {
    const Path resolved = resolveExecutable(command, std::move(services));
    std::error_code error;
    return !resolved.empty() && std::filesystem::exists(resolved, error) && !error
        ? resolved : Path{};
}

template <typename Resolver>
ImageDataWithScale createImage(const Skin& skin, const StringView legacyEngine,
                               const StringView resourceName,
                               const std::uint32_t targetScale,
                               Resolver&& resolver, Services services = {}) {
    ::cwxeditor::cwx::perf::TraceScope trace_dskin_d267(__FILE__, __LINE__);
    (void)skin;
    MaskType maskType = MaskType::NoMask;
    const String path = resolver(maskType);
    if (!legacyEngine.empty() && !resourceName.empty()) {
        ::cwxeditor::cwx::perf::TraceScope trace_dskin_d271(__FILE__, __LINE__);
        auto image = resourceImage(legacyEngine, resourceName, targetScale,
                                   maskType, services);
        if (image.valid()) return image;
    }
    return decodeString(path, targetScale, maskType, std::move(services));
}

template <typename Resolver>
ImageDataWithScale createSkinImage(const Skin& skin,
                                   const std::uint32_t targetScale,
                                   Resolver&& resolver) {
    return createImage(skin, {}, {}, targetScale,
                       std::forward<Resolver>(resolver));
}

}  // namespace

SkinPtr createClassicSkin(const Props& prop, const ClassicEngine& engine) {
    ::cwxeditor::cwx::perf::TraceScope trace_dskin_d24(__FILE__, __LINE__);
    auto skin = Skin::createLegacySkin(
        prop.parent(), prop.enginePath(), engine.enginePath, engine.dataDirName,
        engine.execute, lib::Array<ClassicEngine>{engine},
        String(prop.var().defaultSkin.value));
    if (skin) skin->initialize();
    return skin;
}

bool findCWPy(Props& prop, const StringView scenarioPath) {
    ::cwxeditor::cwx::perf::TraceScope trace_dskin_d30(__FILE__, __LINE__);
    auto& settings = prop.var();
    if (!settings.findEnginePath.value || !settings.enginePath.value.empty()) return false;
    {
    ::cwxeditor::cwx::perf::TraceScope trace_dskin_d31(__FILE__, __LINE__);
    String found = Skin::findCardWirthPy(
        scenarioPath, settings.engine.value, settings.dataDir.value);
    if (found.empty()) {
        found = Skin::findCardWirthPy(
            scenarioPath, settings.engineScript.value, settings.dataDir.value);
    }
    if (found.empty()) return false;
    settings.enginePath = found;
    settings.findEnginePath = false;
    return true;
    }
}

SkinPtr findSkin2(const Props& prop, StringView type, StringView name) {
    ::cwxeditor::cwx::perf::TraceScope trace_dskin_d48(__FILE__, __LINE__);
    const auto& settings = prop.var();
    const String wanted = type.empty() ? String(settings.defaultSkin.value) : String(type);
    const String wantedName(name);
    SkinPtr first;
    const auto skins = skinTable(prop);
    // 共通版の意図的な選択規則。名前未指定では、Dの連想配列
    // の任意の先頭ではなく、設定された既定スキンを優先する。
    if (wantedName.empty()) {
        const auto preferred = std::find_if(skins.begin(), skins.end(), [&prop](const auto& entry) {
            return ::cwxeditor::cwx::utils::cfnmatch(entry.first, prop.parent().defaultSkinPath());
        });
        if (preferred != skins.end() && preferred->second && preferred->second->type() == wanted) {
            preferred->second->initialize();
            return preferred->second;
        }
    }
    for (const auto& entry : skins) {
        ::cwxeditor::cwx::perf::TraceScope trace_dskin_d51(__FILE__, __LINE__);
        const auto& candidate = entry.second;
        if (!candidate || candidate->type() != wanted) continue;
        {
            ::cwxeditor::cwx::perf::TraceScope trace_dskin_d52(__FILE__, __LINE__);
        if (!first) first = candidate;
        if (wantedName.empty() || candidate->name() == wantedName) {
            ::cwxeditor::cwx::perf::TraceScope trace_dskin_d54(__FILE__, __LINE__);
            candidate->initialize();
            return candidate;
        }
        }
    }
    if (first) {
    ::cwxeditor::cwx::perf::TraceScope trace_dskin_d60(__FILE__, __LINE__);
        first->initialize();
        return first;
    }

    return Skin::emptySkin(prop.parent(), prop.enginePath(), wanted);
}

bool hasSkin(const Props& prop, StringView type) {
    ::cwxeditor::cwx::perf::TraceScope trace_dskin_d73(__FILE__, __LINE__);
    for (const auto& entry : skinTable(prop)) {
        ::cwxeditor::cwx::perf::TraceScope trace_dskin_d74(__FILE__, __LINE__);
        if (entry.second && entry.second->type() == type) return true;
    }
    return false;
}

Skin::Table skinTable(const Props& prop) {
    ::cwxeditor::cwx::perf::TraceScope trace_dskin_d79(__FILE__, __LINE__);
    return Skin::table(prop.parent(), prop.enginePath());
}

lib::Array<String> findSkinTypes(const Props& prop) {
    ::cwxeditor::cwx::perf::TraceScope trace_dskin_d83(__FILE__, __LINE__);
    const auto& settings = prop.var();
    std::unordered_set<String> unique;
    unique.emplace(String(settings.defaultSkin.value));
    for (const auto& entry : skinTable(prop)) {
        ::cwxeditor::cwx::perf::TraceScope trace_dskin_d86(__FILE__, __LINE__);
        if (entry.second) {
            const String value = entry.second->type();
            if (!value.empty()) unique.emplace(value);
        }
    }
    for (const auto& engine : settings.classicEngines.value) {
        ::cwxeditor::cwx::perf::TraceScope trace_dskin_d89(__FILE__, __LINE__);
        if (!engine.type.empty()) unique.emplace(engine.type);
    }
    for (const auto& setting : settings.settingsWithSkinTypes.value.values) {
        ::cwxeditor::cwx::perf::TraceScope trace_dskin_d92(__FILE__, __LINE__);
        if (!setting.type.empty()) unique.emplace(setting.type);
    }
    for (const auto& setting : settings.skinTypesByClassicEngines.value) {
        ::cwxeditor::cwx::perf::TraceScope trace_dskin_d95(__FILE__, __LINE__);
        if (!setting.type.empty()) unique.emplace(setting.type);
    }

    lib::Array<String> result;
    result.reserve(unique.size());
    for (auto& value : unique) result.push_back(std::move(value));
    if (settings.logicalSort.value) {
        ::cwxeditor::cwx::perf::TraceScope trace_dskin_d99(__FILE__, __LINE__);
        std::sort(result.begin(), result.end(),
                  [](const String& first, const String& second) {
                      return ::cwxeditor::cwx::utils::ncmp(first, second) < 0;
                  });
    } else {
        ::cwxeditor::cwx::perf::TraceScope trace_dskin_d101(__FILE__, __LINE__);
        std::sort(result.begin(), result.end());
    }
    return result;
}

ImageDataWithScale imgd(StringView path, std::uint32_t targetScale,
                        MaskType maskType,Services services) {
    ::cwxeditor::cwx::perf::TraceScope trace_dskin_d107(__FILE__, __LINE__);
    return decodeString(path,targetScale,maskType,std::move(services));
}






ImageDataWithScale loadIcon(StringView executable, int width, int height,
                            SyncExec syncExec, lib::Services services) {
    ::cwxeditor::cwx::perf::TraceScope trace_dskin_d152(__FILE__, __LINE__);
    services = platformServices(std::move(services));
    ImageDataWithScale result;
    const auto load = [&]() {
        if (!services.graphics) return;
        const lib::Size requested{
            static_cast<float>(std::max(1, width)),
            static_cast<float>(std::max(1, height))};
        const auto resolved = resolveExistingExecutable(executable, services);
        if (resolved.empty()) return;
        const auto resource = services.graphics->load_application_icon(resolved, requested);
        if (!resource) return;
        result.resource = resource;
        result.logical_size = requested;
        result.loaded = true;
        result.source_scale = 1U;
    };
    if (syncExec) {
        ::cwxeditor::cwx::perf::TraceScope trace_dskin_d187(__FILE__, __LINE__); syncExec(std::function<void()>(load)); }
    else {
        ::cwxeditor::cwx::perf::TraceScope trace_dskin_d189(__FILE__, __LINE__); load(); }
    return result;
}

void loadIconAsync(dprops::Images& images, StringView executable,
                   const int width, const int height, Services services,
                   std::function<void(lib::ResourceId)> completed,
                   std::function<bool()> still_needed) {
    if (!completed) return;
    const lib::Size requested{static_cast<float>(std::max(1, width)),
                              static_cast<float>(std::max(1, height))};
    const auto resolved = resolveExistingExecutable(executable, std::move(services));
    if (resolved.empty()) {
        completed(0);
        return;
    }
    images.requestApplicationIcon(resolved, requested, std::move(completed),
                                  std::move(still_needed));
}

ImageDataWithScale imgr(StringView legacyEngine, StringView resourceName,
                        std::uint32_t targetScale, MaskType maskType,Services services) {
    ::cwxeditor::cwx::perf::TraceScope trace_dskin_d196(__FILE__, __LINE__);
    return resourceImage(legacyEngine,resourceName,targetScale,maskType,std::move(services));
}

ImageDataWithScale loadBgImage(const Props& prop, const Skin& skin,
                               const Summary& summary, StringView path,
                               std::uint32_t targetScale) {
    ::cwxeditor::cwx::perf::TraceScope trace_dskin_d262(__FILE__, __LINE__);
    if (!skin.isBgImage(path)) return {};
    auto decoded = ::cwxeditor::cwx::editor::gui::dwt::dutils::loadImageWithScale(
        &prop, &skin, &summary, path, targetScale, false, true, 0, 0, {},
        prop.images().services());
    const auto scale = decoded.scale();
    return ImageDataWithScale(std::move(decoded.baseData()), scale);
}

ImageDataWithScale summary(const Skin& skin, std::uint32_t targetScale,
                           Services services) {
    return createImage(skin, {}, {}, targetScale,
        [&skin](MaskType& mask) { return skin.resSummary(mask); },
        std::move(services));
}

ImageDataWithScale menuCard(const Skin& skin, std::uint32_t targetScale,Services services) {
    return createImage(skin, skin.legacyEngine(), "CARD_NORMAL", targetScale,
        [&skin](MaskType& mask) { return skin.resMenuCard(mask); },std::move(services));
}

ImageDataWithScale castCard(const Skin& skin, std::uint32_t targetScale, Services services) {
    return createImage(skin, skin.legacyEngine(), "CARD_LARGE", targetScale,
        [&skin](MaskType& mask) { return skin.resCastCard(mask); }, std::move(services));
}

ImageDataWithScale castCardInjury(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "CARD_INJURY", targetScale,
        [&skin](MaskType& mask) { return skin.resCastCardInjury(mask); });
}

ImageDataWithScale castCardDanger(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "CARD_DANGER", targetScale,
        [&skin](MaskType& mask) { return skin.resCastCardDanger(mask); });
}

ImageDataWithScale castCardFaint(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "CARD_FAINT", targetScale,
        [&skin](MaskType& mask) { return skin.resCastCardFaint(mask); });
}

ImageDataWithScale castCardBind(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "CARD_BIND", targetScale,
        [&skin](MaskType& mask) { return skin.resCastCardBind(mask); });
}

ImageDataWithScale castCardParaly(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "CARD_PARALY", targetScale,
        [&skin](MaskType& mask) { return skin.resCastCardParaly(mask); });
}

ImageDataWithScale castCardPetrif(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "CARD_PETRIF", targetScale,
        [&skin](MaskType& mask) { return skin.resCastCardPetrif(mask); });
}

ImageDataWithScale castCardSleep(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "CARD_SLEEP", targetScale,
        [&skin](MaskType& mask) { return skin.resCastCardSleep(mask); });
}

ImageDataWithScale lifeBar(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "STATUS_LIFEBAR", targetScale,
        [&skin](MaskType& mask) { return skin.resLifeBar(mask); });
}

ImageDataWithScale lifeGuage(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "STATUS_LIFEGUAGE", targetScale,
        [&skin](MaskType& mask) { return skin.resLifeGuage(mask); });
}

ImageDataWithScale lifeGuage2(const Skin& skin, std::uint32_t targetScale) {
    return createSkinImage(skin, targetScale,
        [&skin](MaskType& mask) { return skin.resLifeGuage2(mask); });
}

ImageDataWithScale lifeGuage2Mask(const Skin& skin, std::uint32_t targetScale) {
    return createSkinImage(skin, targetScale,
        [&skin](MaskType& mask) { return skin.resLifeGuage2Mask(mask); });
}

ImageDataWithScale enhanceUp(const Skin& skin, std::uint32_t targetScale,
                             Enhance value) {
    ::cwxeditor::cwx::perf::TraceScope trace_dskin_d294(__FILE__, __LINE__);
    String resource;
    switch (value) {
    case Enhance::Action: resource = "STATUS_UP0"; break;
    case Enhance::Avoid: resource = "STATUS_UP1"; break;
    case Enhance::Resist: resource = "STATUS_UP2"; break;
    case Enhance::Defense: resource = "STATUS_UP3"; break;
    default: throw std::out_of_range("invalid Enhance value");
    }
    return createImage(skin, skin.legacyEngine(), resource, targetScale,
        [&skin, value](MaskType& mask) { return skin.resEnhanceUp(mask, value); });
}

ImageDataWithScale enhanceDown(const Skin& skin, std::uint32_t targetScale,
                               Enhance value) {
    ::cwxeditor::cwx::perf::TraceScope trace_dskin_d305(__FILE__, __LINE__);
    String resource;
    switch (value) {
    case Enhance::Action: resource = "STATUS_DOWN0"; break;
    case Enhance::Avoid: resource = "STATUS_DOWN1"; break;
    case Enhance::Resist: resource = "STATUS_DOWN2"; break;
    case Enhance::Defense: resource = "STATUS_DOWN3"; break;
    default: throw std::out_of_range("invalid Enhance value");
    }
    return createImage(skin, skin.legacyEngine(), resource, targetScale,
        [&skin, value](MaskType& mask) { return skin.resEnhanceDown(mask, value); });
}

ImageDataWithScale mentality(const Skin& skin, std::uint32_t targetScale,
                             Mentality value) {
    ::cwxeditor::cwx::perf::TraceScope trace_dskin_d316(__FILE__, __LINE__);
    String resource;
    switch (value) {
    case Mentality::Normal: resource = "STATUS_MIND0"; break;
    case Mentality::Sleep: resource = "STATUS_MIND1"; break;
    case Mentality::Confuse: resource = "STATUS_MIND2"; break;
    case Mentality::Overheat: resource = "STATUS_MIND3"; break;
    case Mentality::Brave: resource = "STATUS_MIND4"; break;
    case Mentality::Panic: resource = "STATUS_MIND5"; break;
    default: throw std::out_of_range("invalid Mentality value");
    }
    return createImage(skin, skin.legacyEngine(), resource, targetScale,
        [&skin, value](MaskType& mask) { return skin.resMentality(mask, value); });
}

ImageDataWithScale bind(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "STATUS_MAGIC0", targetScale,
        [&skin](MaskType& mask) { return skin.resBind(mask); });
}

ImageDataWithScale silence(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "STATUS_MAGIC1", targetScale,
        [&skin](MaskType& mask) { return skin.resSilence(mask); });
}

ImageDataWithScale faceUp(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "STATUS_MAGIC2", targetScale,
        [&skin](MaskType& mask) { return skin.resFaceUp(mask); });
}

ImageDataWithScale antiMagic(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "STATUS_MAGIC3", targetScale,
        [&skin](MaskType& mask) { return skin.resAntiMagic(mask); });
}

ImageDataWithScale paralyze(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "STATUS_BODY1", targetScale,
        [&skin](MaskType& mask) { return skin.resParalyze(mask); });
}

ImageDataWithScale poison(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "STATUS_BODY0", targetScale,
        [&skin](MaskType& mask) { return skin.resPoison(mask); });
}

ImageDataWithScale summon(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "STATUS_SUMMON", targetScale,
        [&skin](MaskType& mask) { return skin.resSummon(mask); });
}

ImageDataWithScale itemCard(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "CARD_ITEM", targetScale,
        [&skin](MaskType& mask) { return skin.resItemCard(mask); });
}

ImageDataWithScale skillCard(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "CARD_SKILL", targetScale,
        [&skin](MaskType& mask) { return skin.resSkillCard(mask); });
}

ImageDataWithScale beastCard(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "CARD_BEAST", targetScale,
        [&skin](MaskType& mask) { return skin.resBeastCard(mask); });
}

ImageDataWithScale optionCard(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "CARD_OPTION", targetScale,
        [&skin](MaskType& mask) { return skin.resOptionCard(mask); });
}

ImageDataWithScale infoCard(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "CARD_INFO", targetScale,
        [&skin](MaskType& mask) { return skin.resInfoCard(mask); });
}

ImageDataWithScale cardHold(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "SIGN_HOLD", targetScale,
        [&skin](MaskType& mask) { return skin.resCardHold(mask); });
}

ImageDataWithScale cardPenalty(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "SIGN_PENALTY", targetScale,
        [&skin](MaskType& mask) { return skin.resCardPenalty(mask); });
}

ImageDataWithScale rare(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "SIGN_RARE", targetScale,
        [&skin](MaskType& mask) { return skin.resRare(mask); });
}

ImageDataWithScale premier(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "SIGN_PREMIER", targetScale,
        [&skin](MaskType& mask) { return skin.resPremier(mask); });
}

ImageDataWithScale aptVeryHigh(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "STONE_HAND3", targetScale,
        [&skin](MaskType& mask) { return skin.resAptVeryHigh(mask); });
}

ImageDataWithScale aptHigh(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "STONE_HAND2", targetScale,
        [&skin](MaskType& mask) { return skin.resAptHigh(mask); });
}

ImageDataWithScale aptNormal(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "STONE_HAND1", targetScale,
        [&skin](MaskType& mask) { return skin.resAptNormal(mask); });
}

ImageDataWithScale aptLow(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "STONE_HAND0", targetScale,
        [&skin](MaskType& mask) { return skin.resAptLow(mask); });
}

ImageDataWithScale use0(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "STONE_HAND5", targetScale,
        [&skin](MaskType& mask) { return skin.resUse0(mask); });
}

ImageDataWithScale use1(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "STONE_HAND6", targetScale,
        [&skin](MaskType& mask) { return skin.resUse1(mask); });
}

ImageDataWithScale use2(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "STONE_HAND7", targetScale,
        [&skin](MaskType& mask) { return skin.resUse2(mask); });
}

ImageDataWithScale use3(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "STONE_HAND8", targetScale,
        [&skin](MaskType& mask) { return skin.resUse3(mask); });
}

ImageDataWithScale use4(const Skin& skin, std::uint32_t targetScale) {
    return createImage(skin, skin.legacyEngine(), "STONE_HAND9", targetScale,
        [&skin](MaskType& mask) { return skin.resUse4(mask); });
}

ImageDataWithScale spChar(const Skin& skin, std::uint32_t targetScale,
                          char32_t character, lib::Services services) {
    String resource;
    switch (character) {
    case U'A': case U'a': resource = "FONT_ANGRY"; break;
    case U'B': case U'b': resource = "FONT_CLUB"; break;
    case U'D': case U'd': resource = "FONT_DIAMOND"; break;
    case U'E': case U'e': resource = "FONT_EASY"; break;
    case U'F': case U'f': resource = "FONT_FLY"; break;
    case U'G': case U'g': resource = "FONT_GRIEVE"; break;
    case U'H': case U'h': resource = "FONT_HEART"; break;
    case U'J': case U'j': resource = "FONT_JACK"; break;
    case U'K': case U'k': resource = "FONT_KISS"; break;
    case U'L': case U'l': resource = "FONT_LAUGH"; break;
    case U'N': case U'n': resource = "FONT_NIKO"; break;
    case U'O': case U'o': resource = "FONT_ONSEN"; break;
    case U'P': case U'p': resource = "FONT_PUZZLE"; break;
    case U'Q': case U'q': resource = "FONT_QUICK"; break;
    case U'S': case U's': resource = "FONT_SPADE"; break;
    case U'W': case U'w': resource = "FONT_WORRY"; break;
    case U'X': case U'x': resource = "FONT_X"; break;
    case U'Z': case U'z': resource = "FONT_ZAP"; break;
    default: break;
    }

    return createImage(skin, skin.legacyEngine(), resource, targetScale,
        [&skin, character](MaskType& mask) {
            const auto found = skin.spChars().find(character);
            if (found == skin.spChars().end()) {
                mask = MaskType::NoMask;
                return String{};
            }
            mask = MaskType::NormalMask;
            return found->second;
        }, std::move(services));
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::dskin
