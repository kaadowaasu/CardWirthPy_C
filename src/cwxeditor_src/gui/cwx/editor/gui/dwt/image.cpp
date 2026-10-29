#include "cwxeditor_src/gui/cwx/editor/gui/dwt/image.hpp"
#include "cwxeditor_src/lib/image_mask.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"

#include "cwxeditor_src/core/cwx/structs.hpp"
#include "platform/storage/app_paths.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::image {

namespace {

struct ImageSource {
    std::filesystem::path path;
    std::vector<std::byte> encoded;
    std::uint32_t scale{1U};

    [[nodiscard]] bool isEncoded() const noexcept { return !encoded.empty(); }
};

[[nodiscard]] std::size_t bytesPerPixel(const lib::PixelFormat format) noexcept {
    switch (format) {
    case lib::PixelFormat::Gray8: return 1U;
    case lib::PixelFormat::RGB24:
    case lib::PixelFormat::BGR24: return 3U;
    case lib::PixelFormat::RGBA32:
    case lib::PixelFormat::BGRA32: return 4U;
    default: return 0U;
    }
}

[[nodiscard]] bool regularFile(const std::filesystem::path& path) noexcept {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}

[[nodiscard]] String scaledName(const StringView path, const std::uint32_t scale) {
    if (scale <= 1U) return String(path);
    const String value(path);
    const auto slash = value.find_last_of("/\\");
    auto dot = value.find_last_of('.');
    if (dot == String::npos || (slash != String::npos && dot < slash)) dot = value.size();
    return value.substr(0U, dot) + ".x" + std::to_string(scale) + value.substr(dot);
}

[[nodiscard]] std::vector<std::pair<String, std::uint32_t>> scaledCandidates(
    const StringView path, const std::uint32_t targetScale) {
    // この一覧はD版のモジュールの IMAGE_SCALES と同期させる。 利
    // 用可能な大きい倍率から試すことで、二の累乗でない画面倍率にも対応する （例
    // えば targetScale == 3 なら x2 の素材を優先する）。
    std::vector<std::pair<String, std::uint32_t>> result;
    for (const auto scale : {16U, 8U, 4U, 2U}) {
        if (scale <= targetScale) result.emplace_back(scaledName(path, scale), scale);
    }
    result.emplace_back(String(path), 1U);
    return result;
}

[[nodiscard]] lib::ImageFormat formatHint(const std::filesystem::path& path) noexcept {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (extension == ".bmp") return lib::ImageFormat::Bitmap;
    if (extension == ".png" || extension == ".apng") return lib::ImageFormat::Png;
    if (extension == ".jpg" || extension == ".jpeg") return lib::ImageFormat::Jpeg;
    if (extension == ".gif") return lib::ImageFormat::Gif;
    if (extension == ".webp") return lib::ImageFormat::WebP;
    if (extension == ".ico") return lib::ImageFormat::Icon;
    if (extension == ".cur") return lib::ImageFormat::Cursor;
    return lib::ImageFormat::Unknown;
}

[[nodiscard]] std::filesystem::path appResourceDirectory(const StringView appPath) {
    const auto executable = structs::pathFromUtf8(appPath);
    if (executable.empty()) return std::filesystem::current_path() / "resource";
    return executable.parent_path() / "resource";
}

[[nodiscard]] std::filesystem::path sourceResourceDirectory() {
    // C++版のソースツリーには、取り込んだ標準のエディタ画像がある。 こ
    // こは開発・パッケージ作成時の代替経路に限る。インストール済みの構成で
    // は、 実行ファイルの隣の resource/ または SystemBackend
    // が提供する実行ファイル内の RCDATA を使う。
    auto directory = std::filesystem::path(__FILE__).parent_path();
    for (int i = 0; i < 6 && !directory.empty(); ++i) directory = directory.parent_path();
    return directory / "cwxeditor_src" / "resource";
}

[[nodiscard]] std::filesystem::path sourceEditorDirectory() {
    return sourceResourceDirectory().parent_path();
}

[[nodiscard]] std::optional<ImageSource> findFileSource(const std::filesystem::path& directory,
                                                        const StringView path,
                                                        const std::uint32_t targetScale) {
    for (const auto& [candidate, scale] : scaledCandidates(path, targetScale)) {
        const auto source = directory / structs::pathFromUtf8(candidate);
        if (regularFile(source)) return ImageSource{source, {}, scale};
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ImageSource> findEmbeddedSource(
    const std::shared_ptr<lib::SystemBackend>& system,
    const std::filesystem::path& executable, const StringView path,
    const std::uint32_t targetScale) {
    if (!system) return std::nullopt;
    for (const auto& [candidate, scale] : scaledCandidates(path, targetScale)) {
        // ここではプラットフォーム固有のリソースハンドルではなく、意図的に
        // RCDATA を使う。 Windows の SystemBackend
        // が実装し、他のバックエンドでは省略可能な機能として扱う。
        for (const auto& name : {candidate, String("resource/") + candidate}) {
            std::string error;
            try {
                auto bytes = system->executable_resource(executable, "RCDATA", name, error);
                if (!bytes.empty()) {
                    return ImageSource{structs::pathFromUtf8(candidate), std::move(bytes), scale};
                }
            } catch (...) {
                // 任意の埋め込みリソースがなくても、ソースツリーからの読
                // み込みや 後続のコーデックの試行を妨げないようにする。
            }
        }
    }
    return std::nullopt;
}

void applyColorKey(lib::ImageData& image) {
    lib::apply_image_color_key(image);
}

[[nodiscard]] lib::ImageData scaleImage(const lib::ImageData& source,
                                        const std::uint32_t sourceScale,
                                        const std::uint32_t targetScale) {
    if (sourceScale == targetScale || source.frames.empty()) return source;
    const auto from = std::max(1U, sourceScale);
    const auto to = std::max(1U, targetScale);
    lib::ImageData result = source;
    for (auto& frame : result.frames) {
        const auto bpp = bytesPerPixel(frame.pixel_format);
        if (bpp == 0U || frame.pixel_width == 0U || frame.pixel_height == 0U) continue;
        const auto width64 = static_cast<std::uint64_t>(frame.pixel_width) * to / from;
        const auto height64 = static_cast<std::uint64_t>(frame.pixel_height) * to / from;
        const auto width = static_cast<std::uint32_t>(std::clamp<std::uint64_t>(
            width64, 1U, std::numeric_limits<std::uint32_t>::max()));
        const auto height = static_cast<std::uint32_t>(std::clamp<std::uint64_t>(
            height64, 1U, std::numeric_limits<std::uint32_t>::max()));
        const auto sourcePixels = static_cast<std::size_t>(frame.pixel_width) * frame.pixel_height;
        if (sourcePixels > std::numeric_limits<std::size_t>::max() / bpp ||
            frame.pixels.size() < sourcePixels * bpp) {
            frame.pixel_width = width;
            frame.pixel_height = height;
            frame.logical_size = {static_cast<float>(width), static_cast<float>(height)};
            continue;
        }
        const auto targetPixels = static_cast<std::size_t>(width) * height;
        if (targetPixels > std::numeric_limits<std::size_t>::max() / bpp) continue;
        std::vector<std::byte> pixels(targetPixels * bpp);
        for (std::uint32_t y = 0; y < height; ++y) {
            const auto sourceY = std::min(frame.pixel_height - 1U,
                static_cast<std::uint32_t>(static_cast<std::uint64_t>(y) * frame.pixel_height / height));
            for (std::uint32_t x = 0; x < width; ++x) {
                const auto sourceX = std::min(frame.pixel_width - 1U,
                    static_cast<std::uint32_t>(static_cast<std::uint64_t>(x) * frame.pixel_width / width));
                const auto sourceOffset = (static_cast<std::size_t>(sourceY) * frame.pixel_width + sourceX) * bpp;
                const auto targetOffset = (static_cast<std::size_t>(y) * width + x) * bpp;
                std::copy_n(frame.pixels.data() + sourceOffset, bpp, pixels.data() + targetOffset);
            }
        }
        frame.pixel_width = width;
        frame.pixel_height = height;
        frame.logical_size = {static_cast<float>(width), static_cast<float>(height)};
        frame.pixels = std::move(pixels);
    }
    return result;
}

[[nodiscard]] lib::ImageData decodeSource(const ImageSource& source,
                                          const std::shared_ptr<lib::ImageCodecBackend>& codecs) {
    if (!codecs) return {};
    lib::DecodeImageRequest request;
    request.path = source.path;
    request.format_hint = formatHint(source.path);
    if (source.isEncoded()) request.encoded_data = source.encoded;
    String error;
    try {
        return codecs->decode(request, error);
    } catch (...) {
        return {};
    }
}

}  // namespace

namespace {

[[nodiscard]] bool iconRequestStillNeeded(
    const std::function<bool()>& predicate) noexcept {
    if (!predicate) return true;
    try { return predicate(); } catch (...) { return false; }
}

}  // namespace

struct Images::ApplicationIconState {
    struct Completion {
        std::function<void(Image)> callback;
        std::function<bool()> still_needed;
    };
    struct Pending {
        std::uint64_t token{};
        std::uint64_t epoch{};
        std::vector<Completion> completions;
        std::shared_ptr<lib::TaskHandle> task;
    };
    std::mutex mutex;
    std::uint64_t epoch{1U};
    std::uint64_t next_token{1U};
    std::unordered_map<ApplicationIconKey, Image, ApplicationIconKeyHash> cache;
    std::unordered_map<ApplicationIconKey, Pending, ApplicationIconKeyHash> pending;
    std::unordered_set<Image> owned;
};

void Images::initIcon() {
    if (!icon_.empty()) return;

    const auto scale = defaultScale();
    std::optional<ImageSource> source =
        findFileSource(appResourceDirectory(app_path_), "cwxeditor.ico", scale);
    if (!source) {
        source = findEmbeddedSource(services_.system, structs::pathFromUtf8(app_path_),
                                    "cwxeditor.ico", scale);
    }
    if (!source) source = findFileSource(sourceEditorDirectory(), "cwxeditor.ico", scale);
    if (source && services_.image_codecs && services_.graphics) {
        auto data = decodeSource(*source, services_.image_codecs);
        if (data.frames.empty() && !source->isEncoded()) {
            if (const auto embedded = findEmbeddedSource(services_.system,
                                                         structs::pathFromUtf8(app_path_),
                                                         "cwxeditor.ico", scale)) {
                source = embedded;
                data = decodeSource(*source, services_.image_codecs);
            }
        }
        if (data.frames.empty()) {
            if (const auto file = findFileSource(sourceEditorDirectory(),
                                                 "cwxeditor.ico", scale)) {
                source = file;
                data = decodeSource(*source, services_.image_codecs);
            }
        }
        if (!data.frames.empty()) {
            applyColorKey(data);
            data = scaleImage(data, source->scale, scale);
            std::uint32_t largestWidth = 0U;
            for (std::size_t index = 0; index < data.frames.size(); ++index) {
                lib::ImageData single = data;
                single.frames.clear();
                single.frames.push_back(data.frames[index]);
                try {
                    const Image image = services_.graphics->create_image(single);
                    if (image == 0) continue;
                    owned_images_.insert(image);
                    icon_.push_back(image);
                    if (data.frames[index].pixel_width > largestWidth) {
                        largestWidth = data.frames[index].pixel_width;
                        large_icon_ = image;
                    }
                } catch (...) {
                }
            }
            if (!icon_.empty()) {
                if (large_icon_ == 0) large_icon_ = icon_.back();
                return;
            }
        }
    }

    if (services_.graphics && !app_path_.empty()) {
        try {
            const auto iconSize = std::max(32.0F, 32.0F * static_cast<float>(defaultScale()));
            const Image extracted = services_.graphics->load_application_icon(
                structs::pathFromUtf8(app_path_), {iconSize, iconSize});
            if (extracted != 0) {
                icon_.push_back(extracted);
                large_icon_ = extracted;
                owned_images_.insert(extracted);
            }
        } catch (...) {
        }
    }
}

Images::Images(String appPath, lib::Services services)
    : app_path_(std::move(appPath)), services_(std::move(services)),
      application_icon_state_(std::make_shared<ApplicationIconState>()) {}

Images::~Images() { disposeImages(); }

void Images::requestApplicationIcon(const std::filesystem::path& executable,
                                    const lib::Size requested_size,
                                    std::function<void(Image)> completed,
                                    std::function<bool()> still_needed) {
    if (!completed) return;
    const auto state = application_icon_state_;
    if (!services_.graphics || !services_.tasks || !services_.dispatcher ||
        executable.empty() || !std::isfinite(requested_size.width) ||
        !std::isfinite(requested_size.height) || requested_size.width <= 0.0F ||
        requested_size.height <= 0.0F || requested_size.width > 4096.0F ||
        requested_size.height > 4096.0F) {
        if (iconRequestStillNeeded(still_needed)) completed(0);
        return;
    }

    const auto width = static_cast<std::uint32_t>(std::max(1L, std::lround(requested_size.width)));
    const auto height = static_cast<std::uint32_t>(std::max(1L, std::lround(requested_size.height)));
    std::error_code error;
    auto identity = std::filesystem::absolute(executable, error);
    if (error) identity = executable;
    const auto normalized_identity = identity.lexically_normal();
    // D版の FileCache と同様、正規化したファイルパスと timeLastModified で検証する。
    std::error_code modified_error;
    const auto modified = std::filesystem::last_write_time(normalized_identity, modified_error);
    const auto modified_ticks = modified_error ? std::uint64_t{} :
        static_cast<std::uint64_t>(modified.time_since_epoch().count());
    const ApplicationIconKey key{structs::pathToUtf8(normalized_identity), width, height,
                                 modified_ticks};
    Image cached{};
    std::vector<ApplicationIconState::Completion> cached_completions;
    std::vector<ApplicationIconState::Completion> superseded_completions;
    std::vector<std::shared_ptr<lib::TaskHandle>> superseded_tasks;
    std::uint64_t token{};
    std::uint64_t epoch{};
    std::uint64_t cached_epoch{};
    bool joined_pending{};
    {
        std::scoped_lock lock(state->mutex);
        if (const auto found = state->cache.find(key); found != state->cache.end()) {
            cached = found->second;
            cached_epoch = state->epoch;
        }
        for (auto pending = state->pending.begin(); pending != state->pending.end();) {
            const auto& pending_key = pending->first;
            const bool same_source = pending_key.executable == key.executable &&
                pending_key.width == width && pending_key.height == height;
            if (!same_source || (!cached && pending_key == key)) {
                ++pending;
                continue;
            }
            auto& completions = cached ? cached_completions : superseded_completions;
            completions.reserve(completions.size() + pending->second.completions.size());
            for (auto& completion : pending->second.completions)
                completions.push_back(std::move(completion));
            if (pending->second.task) superseded_tasks.push_back(pending->second.task);
            pending = state->pending.erase(pending);
        }
        if (!cached) {
            if (auto found = state->pending.find(key); found != state->pending.end()) {
                found->second.completions.reserve(
                    found->second.completions.size() + superseded_completions.size() + 1U);
                for (auto& completion : superseded_completions)
                    found->second.completions.push_back(std::move(completion));
                found->second.completions.push_back(
                    {std::move(completed), std::move(still_needed)});
                joined_pending = true;
            } else {
                token = state->next_token++;
                if (token == 0U) token = state->next_token++;
                epoch = state->epoch;
                ApplicationIconState::Pending pending;
                pending.token = token;
                pending.epoch = epoch;
                pending.completions.reserve(superseded_completions.size() + 1U);
                for (auto& completion : superseded_completions)
                    pending.completions.push_back(std::move(completion));
                pending.completions.push_back(
                    {std::move(completed), std::move(still_needed)});
                state->pending.emplace(key, std::move(pending));
            }
        }
    }
    const auto graphics = services_.graphics;
    const auto dispatcher = services_.dispatcher;
    const auto tasks = services_.tasks;
    for (const auto& task : superseded_tasks) if (task) task->cancel();
    if (cached) {
        const auto cached_resource_is_live = [&] {
            std::scoped_lock lock(state->mutex);
            return state->epoch == cached_epoch;
        };
        for (auto& callback : cached_completions) {
            if (!iconRequestStillNeeded(callback.still_needed)) continue;
            if (!cached_resource_is_live()) return;
            try { if (callback.callback) callback.callback(cached); } catch (...) {}
            if (!cached_resource_is_live()) return;
        }
        if (iconRequestStillNeeded(still_needed) && cached_resource_is_live())
            completed(cached);
        return;
    }
    if (joined_pending) return;

    const std::weak_ptr<ApplicationIconState> weak_state = state;
    auto fail_on_caller = [&] {
        std::vector<ApplicationIconState::Completion> callbacks;
        {
            std::scoped_lock lock(state->mutex);
            const auto found = state->pending.find(key);
            if (found == state->pending.end() || found->second.token != token ||
                found->second.epoch != epoch) return;
            callbacks = std::move(found->second.completions);
            state->pending.erase(found);
        }
        for (auto& callback : callbacks) {
            if (!iconRequestStillNeeded(callback.still_needed)) continue;
            try { if (callback.callback) callback.callback(0); } catch (...) {}
        }
    };

    std::shared_ptr<lib::TaskHandle> task;
    try {
        task = tasks->run([weak_state, key, token, epoch, executable,
                           size = lib::Size{static_cast<float>(width), static_cast<float>(height)},
                           graphics, dispatcher] {
            lib::ImageData pixels;
            try { pixels = graphics->extract_application_icon_data(executable, size); }
            catch (...) { pixels = {}; }
            try {
                dispatcher->post([weak_state, key, token, epoch, pixels = std::move(pixels),
                                  graphics, dispatcher]() mutable {
                    const auto shared = weak_state.lock();
                    if (!shared) return;
                    if (!dispatcher->is_main_thread()) {
                        std::scoped_lock lock(shared->mutex);
                        const auto found = shared->pending.find(key);
                        if (found != shared->pending.end() && found->second.token == token &&
                            found->second.epoch == epoch) shared->pending.erase(found);
                        return;
                    }
                    bool has_live_subscriber{};
                    {
                        std::scoped_lock lock(shared->mutex);
                        const auto found = shared->pending.find(key);
                        if (shared->epoch != epoch || found == shared->pending.end() ||
                            found->second.token != token || found->second.epoch != epoch) return;
                        for (const auto& completion : found->second.completions)
                            has_live_subscriber = has_live_subscriber ||
                                iconRequestStillNeeded(completion.still_needed);
                        if (!has_live_subscriber) {
                            shared->pending.erase(found);
                            return;
                        }
                    }

                    Image resource{};
                    if (!pixels.frames.empty()) {
                        try { resource = graphics->create_image(pixels); } catch (...) {}
                    }
                    const Image uploaded = resource;
                    std::vector<ApplicationIconState::Completion> callbacks;
                    bool discard{};
                    {
                        std::scoped_lock lock(shared->mutex);
                        const auto found = shared->pending.find(key);
                        if (shared->epoch != epoch || found == shared->pending.end() ||
                            found->second.token != token || found->second.epoch != epoch) {
                            discard = uploaded != 0;
                        } else {
                            callbacks = std::move(found->second.completions);
                            shared->pending.erase(found);
                            if (uploaded) {
                                // 以前のIDを開いているメニューが借用している場合がある。 disposeImages()
                                // ですべて無効化するまで、各アップロードをこの管理領域で所有する。
                                bool newly_owned{};
                                try {
                                    newly_owned = shared->owned.insert(uploaded).second;
                                    const auto [cached, inserted] =
                                        shared->cache.emplace(key, uploaded);
                                    if (!inserted) {
                                        resource = cached->second;
                                        discard = resource != uploaded;
                                        if (newly_owned) shared->owned.erase(uploaded);
                                    }
                                } catch (...) {
                                    if (newly_owned) shared->owned.erase(uploaded);
                                    discard = true;
                                    resource = 0;
                                }
                            }
                        }
                    }
                    if (discard && uploaded) graphics->destroy_image(uploaded);
                    for (auto& callback : callbacks) {
                        if (!iconRequestStillNeeded(callback.still_needed)) continue;
                        try { if (callback.callback) callback.callback(resource); } catch (...) {}
                    }
                });
            } catch (...) {
                if (const auto shared = weak_state.lock()) {
                    std::scoped_lock lock(shared->mutex);
                    const auto found = shared->pending.find(key);
                    if (found != shared->pending.end() && found->second.token == token &&
                        found->second.epoch == epoch) shared->pending.erase(found);
                }
            }
        });
    } catch (...) {
        fail_on_caller();
        return;
    }
    if (!task) {
        fail_on_caller();
        return;
    }
    {
        std::scoped_lock lock(state->mutex);
        const auto found = state->pending.find(key);
        if (found != state->pending.end() && found->second.token == token &&
            found->second.epoch == epoch) found->second.task = std::move(task);
    }
}

Image Images::imgd(const StringView path, const std::uint32_t targetScale) {
    // D版の必須の同梱 Image とは意図的にAPIを変えている。 任意
    // のリソースがない場合や復号に失敗した場合は、すべてのバックエンドでゼ
    // ロを返す。 テスト用バックエンドでも、成功を装う画素を生成しない。
    try {return loadImage(path,targetScale);}catch(...){return 0;}
}

std::uint32_t Images::defaultScale() const noexcept {
    if (!services_.system) return 1U;
    try {
        const float factor = services_.system->scale_factor();
        if (!std::isfinite(factor) || factor <= 0.0F) return 1U;
        const auto value = std::floor(static_cast<double>(factor));
        if (value >= static_cast<double>(std::numeric_limits<std::uint32_t>::max()))
            return std::numeric_limits<std::uint32_t>::max();
        return std::max(1U, static_cast<std::uint32_t>(value));
    } catch (...) {
        return 1U;
    }
}

Image Images::loadImage(const StringView path, const std::uint32_t targetScale) {
    // 移植版では意図的に、通常のUIリソースはすべての構成で論理的な1倍
    // 座標を占める。 DPIは鮮明な元画像の選択に使い、明示的な targetScale
    // は従来の拡大済み画像のAPIとして維持する。
    const std::uint32_t scale=targetScale?targetScale:1U;
    const std::uint32_t assetScale=targetScale?scale:defaultScale();
    String sourcePath(path);
    const auto file=structs::pathFromUtf8(path);
    const bool external=file.is_absolute();
    bool fileOnly=external;
    if(external) {
        const auto resourceRoot=std::filesystem::absolute(appResourceDirectory(app_path_)).lexically_normal();
        if(cardwirth::platform::path_contains(resourceRoot,file.lexically_normal())) {
            sourcePath=structs::pathToUtf8(file.lexically_relative(resourceRoot));
            fileOnly=false;
        }
    }
    if (path.empty()) throw std::runtime_error("Resource path is empty");
    const ImgRegKey key{sourcePath,scale,assetScale};
    const auto cached = image_registry_.find(key);
    if (cached != image_registry_.end()) return cached->second;

    if (!services_.image_codecs || !services_.graphics)
        throw std::runtime_error("Image services are unavailable for resource: " +
                                 String(path));

    std::optional<ImageSource> source;
    source = findFileSource(appResourceDirectory(app_path_), sourcePath, assetScale);
    if (!source && !fileOnly) {
        const auto executable = structs::pathFromUtf8(app_path_);
        source = findEmbeddedSource(services_.system, executable, sourcePath, assetScale);
    }
    if (!source && !fileOnly) source = findFileSource(sourceResourceDirectory(), sourcePath, assetScale);
    if (!source)
        throw std::runtime_error("Resource not found: " + String(path));

    auto data = decodeSource(*source, services_.image_codecs);
    if (data.frames.empty()) {
        // 古い、または壊れた外部リソースが、有効な埋め込みリソースを隠さないよ
        // うにする。 image.d と同様、失敗を捕捉して代替経路へ進む。
        const auto executable = structs::pathFromUtf8(app_path_);
        if (!fileOnly && !source->isEncoded()) {
            if (const auto embedded = findEmbeddedSource(services_.system, executable, sourcePath, assetScale)) {
                source = embedded;
                data = decodeSource(*source, services_.image_codecs);
            }
        }
    }
    if (data.frames.empty() && !fileOnly) {
        // 生成した取り込みデータのヘッダーが実行ファイルの隣に配置されていな
        // い開発用ビルドでは、 ソースツリーからの代替読み込みを利用できる。
        if (source->isEncoded() || source->path != sourceResourceDirectory() /
                                               structs::pathFromUtf8(sourcePath)) {
            if (const auto file = findFileSource(sourceResourceDirectory(), sourcePath, assetScale)) {
                source = file;
                data = decodeSource(*source, services_.image_codecs);
            }
        }
    }
    if (data.frames.empty())
        throw std::runtime_error("Resource could not be decoded: " + String(path));

    applyColorKey(data);
    data = scaleImage(data, source->scale, scale);
    try {
        const Image result = services_.graphics->create_image(data);
        if (result == 0)
            throw std::runtime_error("Resource could not be created: " + String(path));
        owned_images_.insert(result);
        // 読み込みに成功したリソースだけをキャッシュする。 特に、画
        // 面なしの環境での復号失敗や一時的なバックエンドの不在は、
        // サービスが利用可能になった後で再試行できるようにする。
        decoded_images_.emplace(result, data);
        image_registry_.emplace(key, result);
        return result;
    } catch (const std::exception&) {
        throw;
    } catch (...) {
        throw std::runtime_error("Resource could not be created: " + String(path));
    }
}

lib::Size Images::imageSize(const Image image) const noexcept {
    const auto found = decoded_images_.find(image);
    if (found == decoded_images_.end() || found->second.frames.empty()) return {};
    return found->second.frames.front().logical_size;
}

void Images::disposeImages() noexcept {
    if (application_icon_state_) {
        std::unordered_set<Image> application_icons;
        std::unordered_map<ApplicationIconKey, ApplicationIconState::Pending,
                           ApplicationIconKeyHash> pending;
        {
            std::scoped_lock lock(application_icon_state_->mutex);
            ++application_icon_state_->epoch;
            if (application_icon_state_->epoch == 0U) ++application_icon_state_->epoch;
            pending.swap(application_icon_state_->pending);
            application_icons.swap(application_icon_state_->owned);
            application_icon_state_->cache.clear();
        }
        for (auto& [key, request] : pending) {
            (void)key;
            if (request.task) request.task->cancel();
        }
        if (services_.graphics)
            for (const Image image : application_icons)
                if (image) services_.graphics->destroy_image(image);
    }
    if (services_.graphics) {
        for (const Image resource : owned_images_) {
            if (resource != 0) services_.graphics->destroy_image(resource);
        }
    }
    owned_images_.clear();
    decoded_images_.clear();
    skeleton_images_.clear();
    image_registry_.clear();
    cursor_registry_.clear();
    icon_.clear();
    large_icon_ = 0;
}

Image Images::skeleton(const Image source) {
    if (!source || !services_.graphics) return 0;
    if (const auto found = skeleton_images_.find(source); found != skeleton_images_.end())
        return found->second;
    const auto decoded = decoded_images_.find(source);
    if (decoded == decoded_images_.end()) return 0;
    // D版の directorywindow.d と同様、skeletonImage の派生
    // 画像は一度だけ作る。 元の画素を変更せず、派生画像の所有と解放はこの管理領域で行う。
    Image result{};
    try {
        const auto data = ::cwxeditor::cwx::editor::gui::dwt::dutils::skeletonImage(decoded->second);
        result = services_.graphics->create_image(data);
        if (!result) return 0; // 生成失敗はキャッシュせず、後の呼び出しで再試行できるようにする。
        owned_images_.insert(result);
        skeleton_images_.emplace(source, result);
        return result;
    } catch (...) {
        // imgd と同様、任意のリソースの読み込み失敗は許容す
        // る。 登録途中の派生画像は残さず、元画像も変更しない。
        if (result) {
            owned_images_.erase(result);
            services_.graphics->destroy_image(result);
        }
        return 0;
    }
}

Image Images::emptyIcon() { return imgd("empty.png"); }

Image Images::app() { return imgd("cwxeditor.png"); }

ImageArray Images::icon() {
    initIcon();
    return icon_;
}

Image Images::largeIcon() {
    initIcon();
    return large_icon_;
}

Image Images::toolBar() { return imgd("tool_bar.png"); }

Image Images::toolGroup() { return imgd("tool_group.png"); }

Image Images::text() { return imgd("text.png"); }

Image Images::classicEngine() { return imgd("classic_engine.png"); }

Image Images::warning() { return imgd("warning.png"); }

Image Images::summary() { return imgd("summary.png"); }

Image Images::cards() { return imgd("cards.png"); }

Image Images::cardsWithFlag() { return imgd("cards_flag.png"); }

Image Images::runAwayableEnemyCard() { return imgd("escape.png"); }

Image Images::runAwayableEnemyCardWithFlag() { return imgd("escape_flag.png"); }

Image Images::backs() { return imgd("backs.png"); }

Image Images::backsWithFlag() { return imgd("backs_flag.png"); }

Image Images::colorCell() { return imgd("color_cell.png"); }

Image Images::colorCellWithFlag() { return imgd("color_cell_flag.png"); }

Image Images::textCell() { return imgd("text_cell.png"); }

Image Images::textCellWithFlag() { return imgd("text_cell_flag.png"); }

Image Images::pcCell() { return imgd("pc_cell.png"); }

Image Images::pcCellWithFlag() { return imgd("pc_cell_flag.png"); }

Image Images::imageFont() { return imgd("backs.png"); }

Image Images::inheritBackground() { return imgd("inherit_backs.png"); }

Image Images::notInheritBackground() { return imgd("not_inherit_backs.png"); }

Image Images::bgm() { return imgd("evt_bgm.png"); }

Image Images::se() { return imgd("evt_se.png"); }

Image Images::unknown() { return imgd("unknown.png"); }

Image Images::folder() { return imgd("folder.png"); }

Image Images::scenario() { return imgd("scenario.png"); }

Image Images::areaDir() { return imgd("areadir.png"); }

Image Images::area() { return imgd("area.png"); }

Image Images::battle() { return imgd("battle.png"); }

Image Images::packages() { return imgd("package.png"); }

Image Images::packageDup() { return imgd("package_dup.png"); }

Image Images::startArea() { return imgd("start_area.png"); }

Image Images::areaSceneView() { return imgd("area_cards.png"); }

Image Images::areaSceneViewDup() { return imgd("area_cards_dup.png"); }

Image Images::areaEventTreeView() { return imgd("area_event.png"); }

Image Images::areaEventTreeViewDup() { return imgd("area_event_dup.png"); }

Image Images::battleSceneView() { return imgd("battle_cards.png"); }

Image Images::battleSceneViewDup() { return imgd("battle_cards_dup.png"); }

Image Images::battleEventTreeView() { return imgd("battle_event.png"); }

Image Images::battleEventTreeViewDup() { return imgd("battle_event_dup.png"); }

Image Images::yado() { return imgd("sc_y.png"); }

Image Images::debugYado() { return imgd("debug_yado.png"); }

Image Images::team() { return imgd("sc_t.png"); }

Image Images::playerCardName() { return imgd("sc_pc_name.png"); }

Image Images::selectedPlayerCardNumber() { return imgd("sc_sel_pc_num.png"); }

Image Images::randomValue() { return imgd("random_value.png"); }

Image Images::casts() { return imgd("cast.png"); }

Image Images::skill() { return imgd("skill.png"); }

Image Images::skillDup() { return imgd("skill_dup.png"); }

Image Images::item() { return imgd("item.png"); }

Image Images::itemDup() { return imgd("item_dup.png"); }

Image Images::beast() { return imgd("beast.png"); }

Image Images::beastDup() { return imgd("beast_dup.png"); }

Image Images::info() { return imgd("info.png"); }

Image Images::playerCard() { return imgd("party_cards.png"); }

Image Images::flagDir() { return imgd("flagdir.png"); }

Image Images::flag() { return imgd("flag.png"); }

Image Images::step() { return imgd("step.png"); }

Image Images::variant() { return imgd("variant.png"); }

Image Images::variantRef() { return imgd("variant_ref.png"); }

Image Images::localFlag() { return imgd("flag_l.png"); }

Image Images::localStep() { return imgd("step_l.png"); }

Image Images::localVariant() { return imgd("variant_l.png"); }

Image Images::functions() { return imgd("func.png"); }

Image Images::couponNormal() { return imgd("coupon_n.png"); }

Image Images::couponPlus() { return imgd("coupon_plus.png"); }

Image Images::couponMinus() { return imgd("coupon_minus.png"); }

Image Images::couponHigh() { return imgd("coupon_high.png"); }

Image Images::couponDelete() { return imgd("del_res.png"); }

Image Images::couponSystem() { return imgd("coupon_system.png"); }

Image Images::couponDur() { return imgd("coupon_dur.png"); }

Image Images::couponDurBattle() { return imgd("coupon_dur_battle.png"); }

Image Images::gossip() { return imgd("gossip.png"); }

Image Images::endScenario() { return imgd("end.png"); }

Image Images::evtArrow() { return imgd("evt_arrow.png"); }

Image Images::evtAutoOpen() { return imgd("evt_auto_edit.png"); }

Image Images::evtInsertFirst() { return imgd("evt_insert_first.png"); }

Image Images::couponView() { return imgd("coupon_win.png"); }

Image Images::gossipView() { return imgd("gossip_win.png"); }

Image Images::completeStampView() { return imgd("complete_stamp_win.png"); }

Image Images::keyCodeView() { return imgd("key_code_win.png"); }

Image Images::cellNameView() { return imgd("cell_name_win.png"); }

Image Images::cardGroupView() { return imgd("card_group_win.png"); }

Image Images::actionCard(const types::ActionCardType type) {
    switch (type) {
    case types::ActionCardType::Exchange: return imgd("card_hand.png");
    case types::ActionCardType::Attack: return imgd("hand_attack.png");
    case types::ActionCardType::PowerfulAttack: return imgd("hand_p_attack.png");
    case types::ActionCardType::CriticalAttack: return imgd("hand_c_attack.png");
    case types::ActionCardType::Feint: return imgd("hand_feint.png");
    case types::ActionCardType::Defense: return imgd("hand_defense.png");
    case types::ActionCardType::Distance: return imgd("hand_distance.png");
    case types::ActionCardType::RunAway: return imgd("escape.png");
    case types::ActionCardType::Confuse: return imgd("hand_confuse.png");
    default: return imgd("empty.png");
    }
}

Image Images::content(const types::CType type) {
    switch (type) {
    case types::CType::Start: return imgd("evt_start.png");
    case types::CType::StartBattle: return imgd("evt_battle.png");
    case types::CType::End: return imgd("evt_clear.png");
    case types::CType::EndBadEnd: return imgd("evt_gameover.png");
    case types::CType::ChangeArea: return imgd("evt_area.png");
    case types::CType::ChangeBgImage: return imgd("evt_back.png");
    case types::CType::Effect: return imgd("evt_effect.png");
    case types::CType::EffectBreak: return imgd("evt_stop.png");
    case types::CType::LinkStart: return imgd("evt_link_s.png");
    case types::CType::LinkPackage: return imgd("evt_link_p.png");
    case types::CType::TalkMessage: return imgd("evt_message.png");
    case types::CType::TalkDialog: return imgd("evt_speak.png");
    case types::CType::PlayBgm: return imgd("evt_bgm.png");
    case types::CType::PlaySound: return imgd("evt_se.png");
    case types::CType::Wait: return imgd("evt_wait.png");
    case types::CType::ElapseTime: return imgd("evt_time.png");
    case types::CType::CallStart: return imgd("evt_call_s.png");
    case types::CType::CallPackage: return imgd("evt_call_p.png");
    case types::CType::BranchFlag: return imgd("evt_br_flag.png");
    case types::CType::BranchMultiStep: return imgd("evt_br_step_n.png");
    case types::CType::BranchStep: return imgd("evt_br_step_ul.png");
    case types::CType::BranchSelect: return imgd("evt_br_member.png");
    case types::CType::BranchAbility: return imgd("evt_br_power.png");
    case types::CType::BranchRandom: return imgd("evt_br_random.png");
    case types::CType::BranchLevel: return imgd("evt_br_level.png");
    case types::CType::BranchStatus: return imgd("evt_br_state.png");
    case types::CType::BranchPartyNumber: return imgd("evt_br_num.png");
    case types::CType::BranchArea: return imgd("evt_br_area.png");
    case types::CType::BranchBattle: return imgd("evt_br_battle.png");
    case types::CType::BranchIsBattle: return imgd("evt_br_on_battle.png");
    case types::CType::BranchCast: return imgd("evt_br_cast.png");
    case types::CType::BranchItem: return imgd("evt_br_item.png");
    case types::CType::BranchSkill: return imgd("evt_br_skill.png");
    case types::CType::BranchInfo: return imgd("evt_br_info.png");
    case types::CType::BranchBeast: return imgd("evt_br_beast.png");
    case types::CType::BranchMoney: return imgd("evt_br_money.png");
    case types::CType::BranchCoupon: return imgd("evt_br_coupon.png");
    case types::CType::BranchCompleteStamp: return imgd("evt_br_end.png");
    case types::CType::BranchGossip: return imgd("evt_br_gossip.png");
    case types::CType::SetFlag: return imgd("evt_flag_set.png");
    case types::CType::SetStep: return imgd("evt_step_set.png");
    case types::CType::SetStepUp: return imgd("evt_step_plus.png");
    case types::CType::SetStepDown: return imgd("evt_step_minus.png");
    case types::CType::ReverseFlag: return imgd("evt_flag_r.png");
    case types::CType::CheckFlag: return imgd("evt_flag_judge.png");
    case types::CType::GetCast: return imgd("cast.png");
    case types::CType::GetItem: return imgd("item.png");
    case types::CType::GetSkill: return imgd("skill.png");
    case types::CType::GetInfo: return imgd("info.png");
    case types::CType::GetBeast: return imgd("beast.png");
    case types::CType::GetMoney: return imgd("money.png");
    case types::CType::GetCoupon: return imgd("coupon.png");
    case types::CType::GetCompleteStamp: return imgd("end.png");
    case types::CType::GetGossip: return imgd("gossip.png");
    case types::CType::LoseCast: return imgd("evt_lost_cast.png");
    case types::CType::LoseItem: return imgd("evt_lost_item.png");
    case types::CType::LoseSkill: return imgd("evt_lost_skill.png");
    case types::CType::LoseInfo: return imgd("evt_lost_info.png");
    case types::CType::LoseBeast: return imgd("evt_lost_beast.png");
    case types::CType::LoseMoney: return imgd("evt_lost_money.png");
    case types::CType::LoseCoupon: return imgd("evt_lost_coupon.png");
    case types::CType::LoseCompleteStamp: return imgd("evt_lost_end.png");
    case types::CType::LoseGossip: return imgd("evt_lost_gossip.png");
    case types::CType::ShowParty: return imgd("evt_show_party.png");
    case types::CType::HideParty: return imgd("evt_hide_party.png");
    case types::CType::Redisplay: return imgd("evt_refresh.png");
    case types::CType::SubstituteStep: return imgd("evt_cpstep.png");
    case types::CType::SubstituteFlag: return imgd("evt_cpflag.png");
    case types::CType::BranchStepCmp: return imgd("evt_cmpstep.png");
    case types::CType::BranchFlagCmp: return imgd("evt_cmpflag.png");
    case types::CType::BranchRandomSelect: return imgd("evt_br_rndsel.png");
    case types::CType::BranchKeyCode: return imgd("evt_br_keycode.png");
    case types::CType::CheckStep: return imgd("evt_check_step.png");
    case types::CType::BranchRound: return imgd("evt_br_round.png");
    case types::CType::MoveBgImage: return imgd("evt_mv_back.png");
    case types::CType::ReplaceBgImage: return imgd("evt_rpl_back.png");
    case types::CType::LoseBgImage: return imgd("evt_lose_back.png");
    case types::CType::BranchMultiCoupon: return imgd("evt_br_multi_coupon.png");
    case types::CType::BranchMultiRandom: return imgd("evt_br_multi_random.png");
    case types::CType::MoveCard: return imgd("evt_mv_card.png");
    case types::CType::ChangeEnvironment: return imgd("evt_ch_env.png");
    case types::CType::BranchVariant: return imgd("evt_br_var.png");
    case types::CType::SetVariant: return imgd("evt_set_var.png");
    case types::CType::CheckVariant: return imgd("evt_chk_var.png");
    case types::CType::CustomXmlContent: return imgd("evt_xmlcontent.png");
    default: return 0;
    }
}

Image Images::msnDelete() { return imgd("del_res.png"); }

Image Images::motion(const types::MType type) {
    switch (type) {
    case types::MType::Heal: return imgd("msn_heal.png");
    case types::MType::Damage: return imgd("msn_damage.png");
    case types::MType::Absorb: return imgd("msn_absorb.png");
    case types::MType::Paralyze: return imgd("msn_paralyze.png");
    case types::MType::DisParalyze: return imgd("msn_dis_paralyze.png");
    case types::MType::Poison: return imgd("msn_poison.png");
    case types::MType::DisPoison: return imgd("msn_dis_poison.png");
    case types::MType::GetSkillPower: return imgd("msn_get_skill_power.png");
    case types::MType::LoseSkillPower: return imgd("msn_lose_skill_power.png");
    case types::MType::Sleep: return imgd("msn_sleep.png");
    case types::MType::Confuse: return imgd("msn_confuse.png");
    case types::MType::Overheat: return imgd("msn_overheat.png");
    case types::MType::Brave: return imgd("msn_brave.png");
    case types::MType::Panic: return imgd("msn_panic.png");
    case types::MType::Normal: return imgd("msn_wakeup.png");
    case types::MType::Bind: return imgd("msn_bind.png");
    case types::MType::DisBind: return imgd("msn_dis_bind.png");
    case types::MType::Silence: return imgd("msn_silence.png");
    case types::MType::DisSilence: return imgd("msn_dis_silence.png");
    case types::MType::FaceUp: return imgd("msn_face_up.png");
    case types::MType::FaceDown: return imgd("msn_face_down.png");
    case types::MType::AntiMagic: return imgd("msn_anti_magic.png");
    case types::MType::DisAntiMagic: return imgd("msn_dis_anti_magic.png");
    case types::MType::EnhanceAction: return imgd("msn_enh_action.png");
    case types::MType::EnhanceAvoid: return imgd("msn_enh_avoid.png");
    case types::MType::EnhanceResist: return imgd("msn_enh_resist.png");
    case types::MType::EnhanceDefense: return imgd("msn_enh_defense.png");
    case types::MType::VanishTarget: return imgd("evt_lost_cast.png");
    case types::MType::VanishCard: return imgd("msn_vanish_hand.png");
    case types::MType::VanishBeast: return imgd("evt_lost_beast.png");
    case types::MType::DealAttackCard: return imgd("hand_attack.png");
    case types::MType::DealPowerfulAttackCard: return imgd("hand_p_attack.png");
    case types::MType::DealCriticalAttackCard: return imgd("hand_c_attack.png");
    case types::MType::DealFeintCard: return imgd("hand_feint.png");
    case types::MType::DealDefenseCard: return imgd("hand_defense.png");
    case types::MType::DealDistanceCard: return imgd("hand_distance.png");
    case types::MType::DealConfuseCard: return imgd("hand_confuse.png");
    case types::MType::DealSkillCard: return imgd("hand_skill.png");
    case types::MType::SummonBeast: return imgd("msn_summon.png");
    case types::MType::CancelAction: return imgd("msn_cancel_action.png");
    case types::MType::NoEffect: return imgd("msn_no_effect.png");
    default: return 0;
    }
}

Image Images::element(const types::Element elementValue) {
    switch (elementValue) {
    case types::Element::All: return imgd("elm_all.png");
    case types::Element::Health: return imgd("elm_health.png");
    case types::Element::Mind: return imgd("elm_mind.png");
    case types::Element::Miracle: return imgd("elm_miracle.png");
    case types::Element::Magic: return imgd("elm_magic.png");
    case types::Element::Fire: return imgd("elm_fire.png");
    case types::Element::Ice: return imgd("elm_ice.png");
    default: return 0;
    }
}

Image Images::elementHearing() { return imgd("elm_hearing.png"); }

Image Images::elementVision() { return imgd("elm_vision.png"); }

Image Images::elementElectronic() { return imgd("elm_electronic.png"); }

Image Images::talker(const types::Talker talkerValue) {
    switch (talkerValue) {
    case types::Talker::Selected: return imgd("talker_sel.png");
    case types::Talker::Unselected: return imgd("talker_unsel.png");
    case types::Talker::Random: return imgd("talker_random.png");
    case types::Talker::Valued: return imgd("talker_valued.png");
    case types::Talker::Card:
        throw std::runtime_error("Narration, image and card have no image resource.");
    default: return 0;
    }
}

Image Images::hand() { return imgd("card_hand.png"); }

Image Images::handWith(const std::uint32_t targetScale) {
    return imgd("card_hand.png", targetScale);
}

Image Images::handEmpty() { return imgd("card_hand_empty.png"); }

Image Images::handEmptyWith(const std::uint32_t targetScale) {
    return imgd("card_hand_empty.png", targetScale);
}

Image Images::eventTree() { return imgd("event_tree.png"); }

Image Images::eventTreeWith(const std::uint32_t targetScale) {
    return imgd("event_tree.png", targetScale);
}

Image Images::eventTreeEmpty() { return imgd("event_tree_empty.png"); }

Image Images::eventTreeEmptyWith(const std::uint32_t targetScale) {
    return imgd("event_tree_empty.png", targetScale);
}

Image Images::eventTreeAnd() { return imgd("event_tree_and.png"); }

Image Images::eventTreeSelected() { return imgd("event_tree_selected.png"); }

Image Images::eventTreeAndSelected() { return imgd("event_tree_and_selected.png"); }

Image Images::defStart() { return imgd("def_start.png"); }

Image Images::keyCode() { return imgd("key_code.png"); }

Image Images::round() { return imgd("round.png"); }

Image Images::newIgnition() { return imgd("def_start.png"); }

Image Images::expandTree() { return imgd("tree_open.png"); }

Image Images::foldTree() { return imgd("tree_close.png"); }

Image Images::showEventTreeDetail() { return imgd("evt_detail.png"); }

Image Images::showEventTreeLineNumber() { return imgd("evt_linenum.png"); }

Image Images::addRound() { return imgd("add_round.png"); }

Image Images::delRound() { return imgd("del_round.png"); }

Image Images::keyCodeTiming(const types::FKCKind kind) {
    switch (kind) {
    case types::FKCKind::Use: return imgd("key_code.png");
    case types::FKCKind::Success: return imgd("key_code_suc.png");
    case types::FKCKind::Failure: return imgd("key_code_fail.png");
    case types::FKCKind::HasNot: return imgd("key_code_hasnot.png");
    default: return 0;
    }
}

Image Images::addCoupon() { return imgd("add_coupon.png"); }

Image Images::altCoupon() { return imgd("alt_coupon.png"); }

Image Images::delCoupon() { return imgd("del_res.png"); }

Image Images::addKeyCode() { return imgd("add_key_code.png"); }

Image Images::delKeyCode() { return imgd("del_key_code.png"); }

Image Images::setBeast() { return imgd("set_beast.png"); }

Image Images::sound() { return imgd("evt_se.png"); }

Image Images::setTalkerCoupon() { return imgd("set_beast.png"); }

Image Images::color(const char32_t colorCode) {
    switch (colorCode) {
    case U'W': return imgd("cc_w.png");
    case U'R': return imgd("cc_r.png");
    case U'B': return imgd("cc_b.png");
    case U'G': return imgd("cc_g.png");
    case U'Y': return imgd("cc_y.png");
    case U'O': return imgd("cc_o.png");
    case U'P': return imgd("cc_p.png");
    case U'L': return imgd("cc_l.png");
    case U'D': return imgd("cc_d.png");
    default: return 0;
    }
}

Image Images::scTalker(const types::Talker talkerValue) {
    switch (talkerValue) {
    case types::Talker::Selected: return imgd("sc_m.png");
    case types::Talker::Unselected: return imgd("sc_u.png");
    case types::Talker::Random: return imgd("sc_r.png");
    case types::Talker::Card: return imgd("sc_c.png");
    case types::Talker::Valued:
        throw std::runtime_error("Narration and image have no image resource.");
    default: return 0;
    }
}

Image Images::scRef() { return imgd("sc_i.png"); }

Image Images::scTeam() { return imgd("sc_t.png"); }

Image Images::scYado() { return imgd("sc_y.png"); }

Image Images::summaryFile() { return imgd("summary_file.png"); }

Image Images::scenarioArchive() { return imgd("scenario_arc.png"); }

Image Images::classic() { return imgd("classic.png"); }

Image Images::script() { return imgd("script.png"); }

Image Images::editSceneBattle() { return imgd("battle_cards.png"); }

Image Images::editEventBattle() { return imgd("battle_event.png"); }

Image Images::menu(const types::MenuID id) {
    switch (id) {
    case types::MenuID::DelNotUsedFile: return imgd("del_unuse.png");
    case types::MenuID::CreateSubWindow: return imgd("sub_win.png");
    case types::MenuID::LeftPane: return imgd("left_tab.png");
    case types::MenuID::RightPane: return imgd("right_tab.png");
    case types::MenuID::ClosePane: return imgd("close_pane.png");
    case types::MenuID::ClosePaneExcept: return imgd("close_pane_e.png");
    case types::MenuID::ClosePaneLeft: return imgd("close_pane_l.png");
    case types::MenuID::ClosePaneRight: return imgd("close_pane_r.png");
    case types::MenuID::ClosePaneAll: return imgd("close_pane_a.png");
    case types::MenuID::New: return imgd("new.png");
    case types::MenuID::Open: return imgd("open.png");
    case types::MenuID::NewAtNewWindow: return imgd("new_new_win.png");
    case types::MenuID::OpenAtNewWindow: return imgd("open_new_win.png");
    case types::MenuID::Close: return imgd("close.png");
    case types::MenuID::CloseWin: return imgd("close_win.png");
    case types::MenuID::Save: return imgd("save.png");
    case types::MenuID::SaveAs: return imgd("save_a.png");
    case types::MenuID::Reload: return imgd("reload.png");
    case types::MenuID::EditScenarioHistory: return imgd("edit_sc_hist.png");
    case types::MenuID::EditImportHistory: return imgd("edit_import_hist.png");
    case types::MenuID::EditExecutedPartyHistory: return imgd("edit_party_hist.png");
    case types::MenuID::PutParty: return imgd("sc_t.png");
    case types::MenuID::OpenDir: return imgd("folder.png");
    case types::MenuID::OpenBackupDir: return imgd("open_backup.png");
    case types::MenuID::OpenPlace: return imgd("folder.png");
    case types::MenuID::SaveImage: return imgd("save_inc_img.png");
    case types::MenuID::IncludeImage: return imgd("inc_img.png");
    case types::MenuID::LookImages: return imgd("img_list.png");
    case types::MenuID::EditLayers: return imgd("imagelayer_edit.png");
    case types::MenuID::AddLayer: return imgd("imagelayer_add.png");
    case types::MenuID::RemoveLayer: return imgd("imagelayer_remove.png");
    case types::MenuID::ChangeVH: return imgd("chg_vh.png");
    case types::MenuID::SelectConnectedResource: return imgd("sel_connect.png");
    case types::MenuID::Find: return imgd("replace.png");
    case types::MenuID::FindID: return imgd("find_id.png");
    case types::MenuID::IncSearch: return imgd("inc_search.png");
    case types::MenuID::CloseIncSearch: return imgd("close_win.png");
    case types::MenuID::EditProp: return imgd("edit.png");
    case types::MenuID::ShowProp: return imgd("edit.png");
    case types::MenuID::Refresh: return imgd("refresh.png");
    case types::MenuID::Undo: return imgd("undo.png");
    case types::MenuID::Redo: return imgd("redo.png");
    case types::MenuID::Cut: return imgd("cut.png");
    case types::MenuID::Copy: return imgd("copy.png");
    case types::MenuID::Paste: return imgd("paste.png");
    case types::MenuID::Delete: return imgd("del.png");
    case types::MenuID::Cut1Content: return imgd("cut_1content.png");
    case types::MenuID::Copy1Content: return imgd("copy_1content.png");
    case types::MenuID::Delete1Content: return imgd("del_1content.png");
    case types::MenuID::PasteInsert: return imgd("paste_insert.png");
    case types::MenuID::Clone: return imgd("clone.png");
    case types::MenuID::SelectAll: return imgd("select_all.png");
    case types::MenuID::CopyAll: return imgd("copy_all.png");
    case types::MenuID::ToXMLText: return imgd("toxml.png");
    case types::MenuID::AddItem: return imgd("add_item.png");
    case types::MenuID::DelItem: return imgd("del_item.png");
    case types::MenuID::TableView: return imgd("data_win.png");
    case types::MenuID::VarView: return imgd("flag_win.png");
    case types::MenuID::CardView: return imgd("card_win.png");
    case types::MenuID::CastView: return imgd("cast_win.png");
    case types::MenuID::SkillView: return imgd("skill_win.png");
    case types::MenuID::ItemView: return imgd("item_win.png");
    case types::MenuID::BeastView: return imgd("beast_win.png");
    case types::MenuID::InfoView: return imgd("info_win.png");
    case types::MenuID::FileView: return imgd("dir_win.png");
    case types::MenuID::CouponView: return imgd("coupon_win.png");
    case types::MenuID::GossipView: return imgd("gossip_win.png");
    case types::MenuID::CompleteStampView: return imgd("complete_stamp_win.png");
    case types::MenuID::KeyCodeView: return imgd("key_code_win.png");
    case types::MenuID::CellNameView: return imgd("cell_name_win.png");
    case types::MenuID::CardGroupView: return imgd("card_group_win.png");
    case types::MenuID::ExecEngine: return imgd("exec_engine.png");
    case types::MenuID::ExecEngineAuto: return imgd("exec_engine.png");
    case types::MenuID::ExecEngineMain: return imgd("exec_engine.png");
    case types::MenuID::ExecEngineWithParty: return imgd("exec_engine_with_party.png");
    case types::MenuID::ExecEngineWithLastParty: return imgd("exec_engine_with_party.png");
    case types::MenuID::DeleteNotExistsParties: return imgd("del_not_exists_party.png");
    case types::MenuID::OuterTools: return imgd("outer_tool.png");
    case types::MenuID::Settings: return imgd("settings.png");
    case types::MenuID::VersionInfo: return imgd("version.png");
    case types::MenuID::LockToolBar: return imgd("lock_bar.png");
    case types::MenuID::ResetToolBar: return imgd("reset_bar.png");
    case types::MenuID::CopyAsText: return imgd("copy.png");
    case types::MenuID::OpenAtView: return imgd("view.png");
    case types::MenuID::EventToPackage: return imgd("e_to_p.png");
    case types::MenuID::StartToPackage: return imgd("s_to_p.png");
    case types::MenuID::WrapTree: return imgd("wrap_tree.png");
    case types::MenuID::CreateContent: return imgd("evt_put_quick.png");
    case types::MenuID::ConvertContent: return imgd("conv_cont.png");
    case types::MenuID::CGroupTerminal: return imgd("evt_j_term.png");
    case types::MenuID::CGroupStandard: return imgd("evt_j_std.png");
    case types::MenuID::CGroupData: return imgd("evt_j_data.png");
    case types::MenuID::CGroupUtility: return imgd("evt_j_util.png");
    case types::MenuID::CGroupBranch: return imgd("evt_j_br.png");
    case types::MenuID::CGroupGet: return imgd("evt_j_get.png");
    case types::MenuID::CGroupLost: return imgd("evt_j_lost.png");
    case types::MenuID::CGroupVisual: return imgd("evt_j_vis.png");
    case types::MenuID::CGroupVariant: return imgd("evt_j_var.png");
    case types::MenuID::EditSummary: return imgd("summary.png");
    case types::MenuID::NewAreaDir: return imgd("areadir_new.png");
    case types::MenuID::NewArea: return imgd("area_new.png");
    case types::MenuID::NewBattle: return imgd("battle_new.png");
    case types::MenuID::NewPackage: return imgd("package_new.png");
    case types::MenuID::ReNumberingAll: return imgd("renum_all.png");
    case types::MenuID::ReNumbering: return imgd("renum.png");
    case types::MenuID::EditScene: return imgd("area_cards.png");
    case types::MenuID::EditSceneDup: return imgd("dup_scene.png");
    case types::MenuID::EditEvent: return imgd("area_event.png");
    case types::MenuID::EditEventDup: return imgd("dup_event.png");
    case types::MenuID::SetStartArea: return imgd("start_area.png");
    case types::MenuID::NewFlagDir: return imgd("flagdir_new.png");
    case types::MenuID::NewFlag: return imgd("flag_new.png");
    case types::MenuID::NewStep: return imgd("step_new.png");
    case types::MenuID::NewVariant: return imgd("variant_new.png");
    case types::MenuID::CreateStepValues: return imgd("create_step_values.png");
    case types::MenuID::PutSPChar: return imgd("put_sp_char.png");
    case types::MenuID::PutFlagValue: return imgd("flag.png");
    case types::MenuID::PutStepValue: return imgd("step.png");
    case types::MenuID::PutVariantValue: return imgd("variant.png");
    case types::MenuID::PutColor: return imgd("cc_w.png");
    case types::MenuID::PutSkinSPChar: return imgd("sc_skin.png");
    case types::MenuID::PutImageFont: return imgd("backs.png");
    case types::MenuID::CreateVariableEventTree: return imgd("var_tree.png");
    case types::MenuID::InitVariablesTree: return imgd("var_init.png");
    case types::MenuID::CopyVariablePath: return imgd("copy_path.png");
    case types::MenuID::Up: return imgd("up.png");
    case types::MenuID::Down: return imgd("down.png");
    case types::MenuID::ConvertCouponType: return imgd("coupon_conv.png");
    case types::MenuID::ConvertCouponTypeNormal: return imgd("coupon_n.png");
    case types::MenuID::ConvertCouponTypeHide: return imgd("coupon_hidden.png");
    case types::MenuID::ConvertCouponTypeDur: return imgd("coupon_dur.png");
    case types::MenuID::ConvertCouponTypeDurBattle: return imgd("coupon_dur_battle.png");
    case types::MenuID::ConvertCouponTypeSystem: return imgd("coupon_system.png");
    case types::MenuID::CopyTypeConvertedCoupon: return imgd("coupon_copy_conv.png");
    case types::MenuID::CopyTypeConvertedCouponNormal: return imgd("coupon_n.png");
    case types::MenuID::CopyTypeConvertedCouponHide: return imgd("coupon_hidden.png");
    case types::MenuID::CopyTypeConvertedCouponDur: return imgd("coupon_dur.png");
    case types::MenuID::CopyTypeConvertedCouponDurBattle: return imgd("coupon_dur_battle.png");
    case types::MenuID::CopyTypeConvertedCouponSystem: return imgd("coupon_system.png");
    case types::MenuID::AddInitialCoupons: return imgd("add_initial_coupons.png");
    case types::MenuID::ReverseSignOfValues: return imgd("reverse_sign_of_values.png");
    case types::MenuID::Reverse: return imgd("reverse.png");
    case types::MenuID::SwapToParent: return imgd("swap_parent.png");
    case types::MenuID::SwapToChild: return imgd("swap_child.png");
    case types::MenuID::OverDialog: return imgd("over_dlg.png");
    case types::MenuID::UnderDialog: return imgd("under_dlg.png");
    case types::MenuID::CreateDialog: return imgd("evt_speak.png");
    case types::MenuID::DeleteDialog: return imgd("del_res.png");
    case types::MenuID::CopyToAllDialogs: return imgd("copy_dialog.png");
    case types::MenuID::CopyToUpperDialogs: return imgd("copy_dialog_u.png");
    case types::MenuID::CopyToLowerDialogs: return imgd("copy_dialog_l.png");
    case types::MenuID::ShowParty: return imgd("party_cards.png");
    case types::MenuID::ShowMsg: return imgd("view_msg.png");
    case types::MenuID::ShowRefCards: return imgd("view_ref.png");
    case types::MenuID::FixedCards: return imgd("fixed.png");
    case types::MenuID::FixedCells: return imgd("fixed_cell.png");
    case types::MenuID::FixedBackground: return imgd("fixed_background.png");
    case types::MenuID::ShowGrid: return imgd("grid.png");
    case types::MenuID::ShowEnemyCardProp: return imgd("card_life.png");
    case types::MenuID::ShowCard: return imgd("show_cards.png");
    case types::MenuID::ShowBack: return imgd("show_backs.png");
    case types::MenuID::NewMenuCard: return imgd("card_new.png");
    case types::MenuID::NewEnemyCard: return imgd("card_new.png");
    case types::MenuID::NewBack: return imgd("back_new.png");
    case types::MenuID::NewTextCell: return imgd("text_cell_new.png");
    case types::MenuID::NewColorCell: return imgd("color_cell_new.png");
    case types::MenuID::NewPCCell: return imgd("pc_cell_new.png");
    case types::MenuID::AutoArrange: return imgd("auto.png");
    case types::MenuID::ManualArrange: return imgd("custom.png");
    case types::MenuID::PossibleToRunAway: return imgd("possible_run_away.png");
    case types::MenuID::SortWithPosition: return imgd("sort_with_pos.png");
    case types::MenuID::Mask: return imgd("mask.png");
    case types::MenuID::Escape: return imgd("escape.png");
    case types::MenuID::ChangePos: return imgd("chg_pos.png");
    case types::MenuID::PosTop: return imgd("pos_top.png");
    case types::MenuID::PosBottom: return imgd("pos_bottom.png");
    case types::MenuID::PosLeft: return imgd("pos_left.png");
    case types::MenuID::PosRight: return imgd("pos_right.png");
    case types::MenuID::PosEven: return imgd("pos_even.png");
    case types::MenuID::NearTop: return imgd("near_top.png");
    case types::MenuID::NearBottom: return imgd("near_bottom.png");
    case types::MenuID::NearLeft: return imgd("near_left.png");
    case types::MenuID::NearRight: return imgd("near_right.png");
    case types::MenuID::NearCenterH: return imgd("near_center_h.png");
    case types::MenuID::NearCenterV: return imgd("near_center_v.png");
    case types::MenuID::NearCenter: return imgd("near_center.png");
    case types::MenuID::ScaleMin: return imgd("scale_min.png");
    case types::MenuID::ScaleMiddle: return imgd("scale_middle.png");
    case types::MenuID::ScaleMax: return imgd("scale_max.png");
    case types::MenuID::ScaleBig: return imgd("scale_even_big.png");
    case types::MenuID::ScaleSmall: return imgd("scale_even_small.png");
    case types::MenuID::ExpandBack: return imgd("expand_back.png");
    case types::MenuID::CopyColor1ToColor2: return imgd("copy_color1_to_color2.png");
    case types::MenuID::CopyColor2ToColor1: return imgd("copy_color2_to_color1.png");
    case types::MenuID::ExchangeColors: return imgd("exchange_colors.png");
    case types::MenuID::StopBGM: return imgd("sound_stop.png");
    case types::MenuID::PlayBGM: return imgd("sound_play.png");
    case types::MenuID::NewEvent: return imgd("new_event_tree.png");
    case types::MenuID::NewEventWithDialog: return imgd("new_event_tree.png");
    case types::MenuID::KeyCodeTiming: return imgd("key_code_conv.png");
    case types::MenuID::KeyCodeTimingUse: return imgd("key_code.png");
    case types::MenuID::KeyCodeTimingSuccess: return imgd("key_code_suc.png");
    case types::MenuID::KeyCodeTimingFailure: return imgd("key_code_fail.png");
    case types::MenuID::KeyCodeTimingHasNot: return imgd("key_code_hasnot.png");
    case types::MenuID::CopyTimingConvertedKeyCode: return imgd("key_code_copy_conv.png");
    case types::MenuID::CopyTimingConvertedKeyCodeUse: return imgd("key_code.png");
    case types::MenuID::CopyTimingConvertedKeyCodeSuccess: return imgd("key_code_suc.png");
    case types::MenuID::CopyTimingConvertedKeyCodeFailure: return imgd("key_code_fail.png");
    case types::MenuID::CopyTimingConvertedKeyCodeHasNot: return imgd("key_code_hasnot.png");
    case types::MenuID::AddKeyCodesByFeatures: return imgd("add_key_codes_by_features.png");
    case types::MenuID::KeyCodeCond: return imgd("key_code_cond.png");
    case types::MenuID::KeyCodeCondOr: return imgd("key_code_or.png");
    case types::MenuID::KeyCodeCondAnd: return imgd("key_code_and.png");
    case types::MenuID::AddRangeOfRound: return imgd("add_many_round.png");
    case types::MenuID::OpenAtTableView: return imgd("open_tableview.png");
    case types::MenuID::OpenAtVarView: return imgd("open_flagview.png");
    case types::MenuID::OpenAtCardView: return imgd("open_cardview.png");
    case types::MenuID::OpenAtFileView: return imgd("open_fileview.png");
    case types::MenuID::OpenAtEventView: return imgd("open_eventtreeview.png");
    case types::MenuID::Comment: return imgd("comment.png");
    case types::MenuID::ShowCardProp: return imgd("card_life.png");
    case types::MenuID::ShowCardImage: return imgd("card_list.png");
    case types::MenuID::ShowCardDetail: return imgd("card_table.png");
    case types::MenuID::OpenImportSource: return imgd("add_scenario.png");
    case types::MenuID::SelectImportSource: return imgd("sel_import_target.png");
    case types::MenuID::NewCast: return imgd("cast_new.png");
    case types::MenuID::NewSkill: return imgd("skill_new.png");
    case types::MenuID::NewItem: return imgd("item_new.png");
    case types::MenuID::NewBeast: return imgd("beast_new.png");
    case types::MenuID::NewInfo: return imgd("info_new.png");
    case types::MenuID::Import: return imgd("add.png");
    case types::MenuID::OpenHand: return imgd("card_hand.png");
    case types::MenuID::AddHand: return imgd("add_hand.png");
    case types::MenuID::RemoveRef: return imgd("remove_ref.png");
    case types::MenuID::EditEventAtTimeOfUsing: return imgd("event_tree.png");
    case types::MenuID::Hold: return imgd("hold.png");
    case types::MenuID::PlaySE: return imgd("sound_play.png");
    case types::MenuID::StopSE: return imgd("sound_stop.png");
    case types::MenuID::NewDir: return imgd("folder_new.png");
    case types::MenuID::CopyFilePath: return imgd("copy_path.png");
    case types::MenuID::CreateArchive: return imgd("create_archive.png");
    case types::MenuID::PutQuick: return imgd("evt_put_quick.png");
    case types::MenuID::PutSelect: return imgd("evt_put_select.png");
    case types::MenuID::PutContinue: return imgd("evt_add_continue.png");
    case types::MenuID::ToScript: return imgd("script.png");
    case types::MenuID::ToScriptAll: return imgd("script_all.png");
    case types::MenuID::ToScript1Content: return imgd("script_1content.png");
    case types::MenuID::EvTemplates: return imgd("ev_tmpl.png");
    case types::MenuID::EvTemplatesOfScenario: return imgd("new_ev_tmpl.png");
    case types::MenuID::EditSelection: return imgd("edit_selection.png");
    case types::MenuID::Expand: return imgd("expanded.png");
    case types::MenuID::Collapse: return imgd("collapsed.png");
    case types::MenuID::SelectCurrentEvent: return imgd("select_current_event.png");
    case types::MenuID::ResetValues: return imgd("reset.png");
    case types::MenuID::ResetValuesAll: return imgd("reset_all.png");
    case types::MenuID::CustomizeToolBar: return imgd("custom_tools.png");
    case types::MenuID::AddTool: return imgd("add_menu.png");
    case types::MenuID::AddToolBar: return imgd("add_bar.png");
    case types::MenuID::AddToolGroup: return imgd("add_group.png");
    case types::MenuID::ResetToolBarSettings: return imgd("reset_all.png");
    case types::MenuID::DeleteNotExistsHistory: return imgd("del_not_exists_sc.png");
    case types::MenuID::SelectIcon: return imgd("select_icon.png");
    case types::MenuID::SelectPresetIcon: return imgd("select_preset_icon.png");
    case types::MenuID::DeleteIcon: return imgd("delete_icon.png");
    default: return 0;
    }
}

Cursor Images::cursor(const types::CType type) {
    const auto cached = cursor_registry_.find(type);
    if (cached != cursor_registry_.end()) return cached->second;

    const Image source = content(type);
    if (source == 0) return 0;

    // 共通の描画APIにはカーソル生成機能がないため、元画像のリソースを
    // カーソルのトークンとして保持する。 所有権は画像の管理領域に残す。
    cursor_registry_.emplace(type, source);
    return source;
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::image
