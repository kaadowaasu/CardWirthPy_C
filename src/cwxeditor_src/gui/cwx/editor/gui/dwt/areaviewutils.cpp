#include "cwxeditor_src/gui/cwx/editor/gui/dwt/areaviewutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/areaview.hpp"

#include "cwxeditor_src/core/cwx/flag.hpp"
#include "cwxeditor_src/core/cwx/graphics.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dskin.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/jpyimage.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/messageutils.hpp"
#include "cwxeditor_src/lib/window_host.hpp"
#include "cwxeditor_src/lib/image_text.hpp"
#include "cwxeditor_src/lib/image_composition.hpp"
#include "cwxeditor_src/lib/image_mask.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::areaviewutils {
namespace {

[[nodiscard]] std::uint32_t positiveScale(const std::uint32_t value) noexcept {
    return std::max<std::uint32_t>(1U, value);
}

[[nodiscard]] std::int32_t clampDimension(const float value) noexcept {
    if (value <= 0.0F) return 0;
    if (value >= static_cast<float>(std::numeric_limits<std::int32_t>::max())) {
        return std::numeric_limits<std::int32_t>::max();
    }
    return static_cast<std::int32_t>(std::lround(value));
}

[[nodiscard]] lib::Size imageSize(const lib::ImageData& data) noexcept {
    if (data.frames.empty()) return {};
    const auto& frame = data.frames.front();
    if (frame.logical_size.width > 0.0F && frame.logical_size.height > 0.0F) {
        return frame.logical_size;
    }
    return {static_cast<float>(frame.pixel_width),
            static_cast<float>(frame.pixel_height)};
}

void assignLogicalSize(ImageDataWithScale& value) noexcept {
    if (value.logical_size.width > 0.0F && value.logical_size.height > 0.0F) return;
    const auto size = imageSize(value.data);
    const auto scale = static_cast<float>(positiveScale(value.source_scale));
    value.logical_size = {size.width / scale, size.height / scale};
}

[[nodiscard]] ImageDataWithScale fallbackData(const lib::Size size,
                                              const std::uint32_t source_scale) {
    ImageDataWithScale result;
    result.source_scale = positiveScale(source_scale);
    result.logical_size = size;
    return result;
}

[[nodiscard]] String scenarioPath(const Summary* summary) {
    return summary ? summary->scenarioPath() : String{};
}

[[nodiscard]] String dataVersion(const Summary* summary) {
    return summary ? summary->dataVersion() : String(versioninfo::LATEST_VERSION);
}

[[nodiscard]] bool contains(const lib::Rect& rect, const lib::Point point) noexcept {
    return point.x >= rect.x && point.y >= rect.y &&
           point.x < rect.x + rect.width && point.y < rect.y + rect.height;
}

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

[[nodiscard]] lib::Color pixelAt(const lib::ImageFrame& frame,
                                 const std::uint32_t x,
                                 const std::uint32_t y) noexcept {
    const auto bytes = bytesPerPixel(frame.pixel_format);
    const auto offset = (static_cast<std::size_t>(y) * frame.pixel_width + x) * bytes;
    if (bytes == 0U || offset + bytes > frame.pixels.size()) return {0, 0, 0, 0};
    const auto value = [&](const std::size_t index) {
        return static_cast<std::uint8_t>(frame.pixels[offset + index]);
    };
    switch (frame.pixel_format) {
    case lib::PixelFormat::Gray8: return {value(0), value(0), value(0), 255};
    case lib::PixelFormat::RGB24: return {value(0), value(1), value(2), 255};
    case lib::PixelFormat::BGR24: return {value(2), value(1), value(0), 255};
    case lib::PixelFormat::RGBA32: return {value(0), value(1), value(2), value(3)};
    case lib::PixelFormat::BGRA32: return {value(2), value(1), value(0), value(3)};
    default: return {0, 0, 0, 0};
    }
}

void putPixel(lib::ImageFrame& frame, const std::uint32_t x,
              const std::uint32_t y, const lib::Color color) noexcept {
    const auto offset = (static_cast<std::size_t>(y) * frame.pixel_width + x) * 4U;
    if (offset + 4U > frame.pixels.size()) return;
    const bool rgba = frame.pixel_format == lib::PixelFormat::RGBA32;
    frame.pixels[offset + 0U] = static_cast<std::byte>(rgba ? color.red : color.blue);
    frame.pixels[offset + 1U] = static_cast<std::byte>(color.green);
    frame.pixels[offset + 2U] = static_cast<std::byte>(rgba ? color.blue : color.red);
    frame.pixels[offset + 3U] = static_cast<std::byte>(color.alpha);
}

[[nodiscard]] lib::ImageFrame rgbaFrame(const lib::ImageFrame& source);

[[nodiscard]] bool hasMeaningfulAlpha(const lib::ImageFrame& frame) noexcept {
    if (frame.pixel_format != lib::PixelFormat::RGBA32 &&
        frame.pixel_format != lib::PixelFormat::BGRA32) return false;
    for (std::uint32_t y = 0; y < frame.pixel_height; ++y) {
        for (std::uint32_t x = 0; x < frame.pixel_width; ++x) {
            if (pixelAt(frame, x, y).alpha != 255U) return true;
        }
    }
    return false;
}

void applyTopLeftColorKey(lib::ImageFrame& frame) {
    if (frame.pixel_width == 0U || frame.pixel_height == 0U ||
        hasMeaningfulAlpha(frame)) return;
    const auto key = pixelAt(frame, 0U, 0U);
    auto converted = rgbaFrame(frame);
    for (std::uint32_t y = 0; y < converted.pixel_height; ++y) {
        for (std::uint32_t x = 0; x < converted.pixel_width; ++x) {
            auto pixel = pixelAt(converted, x, y);
            if (pixel.red == key.red && pixel.green == key.green &&
                pixel.blue == key.blue) pixel.alpha = 0U;
            putPixel(converted, x, y, pixel);
        }
    }
    frame = std::move(converted);
}

[[nodiscard]] lib::ImageFrame rgbaFrame(const lib::ImageFrame& source) {
    lib::ImageFrame result;
    result.logical_size = source.logical_size;
    result.pixel_width = source.pixel_width;
    result.pixel_height = source.pixel_height;
    // SDLが使う共通GraphicsBackendはRGBA32を公開
    // する。PileImageをBGRA32のままにすると、合成前は見え
    // ていたメニューカードが、最初の画像レイヤーで合成を必要とした時点で
    // 消えてしまう。合成した場面画像はすべてRGBA32に正規化する。
    result.pixel_format = lib::PixelFormat::RGBA32;
    result.duration = source.duration;
    result.pixels.resize(static_cast<std::size_t>(result.pixel_width) *
                         result.pixel_height * 4U);
    for (std::uint32_t y = 0; y < result.pixel_height; ++y)
        for (std::uint32_t x = 0; x < result.pixel_width; ++x)
            putPixel(result, x, y, pixelAt(source, x, y));
    return result;
}

[[nodiscard]] lib::ImageFrame resizeSceneFrame(const lib::ImageFrame& source,
                                               const std::uint32_t width,
                                               const std::uint32_t height,
                                               const bool smoothing) {
    auto normalized = rgbaFrame(source);
    if (width == 0U || height == 0U || normalized.pixel_width == 0U ||
        normalized.pixel_height == 0U) return {};
    if (normalized.pixel_width == width && normalized.pixel_height == height)
        return normalized;
    const auto source_count = std::uint64_t(normalized.pixel_width) *
                              normalized.pixel_height;
    const auto target_count = std::uint64_t(width) * height;
    if (source_count > std::numeric_limits<std::size_t>::max() / 4U ||
        target_count > std::numeric_limits<std::size_t>::max() / 4U)
        throw std::length_error("Scene image resize is too large");
    lib::Bytes channels(static_cast<std::size_t>(source_count) * 4U);
    lib::Bytes alpha(static_cast<std::size_t>(source_count));
    for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(source_count); ++pixel) {
        const auto offset = pixel * 4U;
        for (std::size_t channel = 0; channel < 4U; ++channel)
            channels[offset + channel] =
                std::to_integer<std::uint8_t>(normalized.pixels[offset + channel]);
        alpha[pixel] = channels[offset + 3U];
        channels[offset + 3U] = 255U;
    }
    std::size_t stride{};
    auto resized = smoothing
        ? ::cwxeditor::cwx::graphics::smoothResize(width, height, channels, alpha,
              32U, normalized.pixel_width, normalized.pixel_height,
              std::size_t(normalized.pixel_width) * 4U, stride)
        : ::cwxeditor::cwx::graphics::resize(width, height, channels, alpha,
              32U, normalized.pixel_width, normalized.pixel_height,
              std::size_t(normalized.pixel_width) * 4U, stride);
    if (stride < std::size_t(width) * 4U ||
        std::size_t(height) > std::numeric_limits<std::size_t>::max() / stride ||
        resized.size() < std::size_t(height) * stride ||
        alpha.size() < static_cast<std::size_t>(target_count))
        throw std::runtime_error("Scene image resize returned incomplete pixels");
    lib::ImageFrame result;
    result.pixel_width = width;
    result.pixel_height = height;
    result.pixel_format = lib::PixelFormat::RGBA32;
    result.duration = normalized.duration;
    result.pixels.resize(static_cast<std::size_t>(target_count) * 4U);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto target = std::size_t(y) * width + x;
            const auto input = std::size_t(y) * stride + std::size_t(x) * 4U;
            const auto output = target * 4U;
            // 共通のサイズ変更処理はBGRの読み書きを対にして、ここでのバイト順を維持する。
            for (std::size_t channel = 0; channel < 3U; ++channel)
                result.pixels[output + channel] = static_cast<std::byte>(resized[input + channel]);
            result.pixels[output + 3U] = static_cast<std::byte>(alpha[target]);
        }
    }
    return result;
}

[[nodiscard]] std::uint32_t scaledSceneDimension(const std::int32_t logical,
                                                 const std::uint32_t scale) noexcept {
    if (logical <= 0) return 0U;
    const auto physical = std::uint64_t(static_cast<std::uint32_t>(logical)) *
                          positiveScale(scale);
    return static_cast<std::uint32_t>(std::clamp<std::uint64_t>(
        physical, 1U, std::numeric_limits<std::uint32_t>::max()));
}

[[nodiscard]] bool resizeResizableBase(ImageDataWithScale& image,
                                       const std::int32_t logical_width,
                                       const std::int32_t logical_height,
                                       const std::uint32_t target_scale,
                                       const bool smoothing) {
    const auto width = scaledSceneDimension(logical_width, target_scale);
    const auto height = scaledSceneDimension(logical_height, target_scale);
    if (width == 0U || height == 0U || image.data.frames.empty()) return false;
    try {
        std::vector<lib::ImageFrame> frames;
        frames.reserve(image.data.frames.size());
        for (const auto& source : image.data.frames) {
            auto frame = resizeSceneFrame(source, width, height, smoothing);
            if (frame.pixel_width == 0U || frame.pixel_height == 0U) return false;
            frame.logical_size = {static_cast<float>(logical_width),
                                  static_cast<float>(logical_height)};
            frames.push_back(std::move(frame));
        }
        image.data.frames = std::move(frames);
        image.data.format = lib::ImageFormat::Bitmap;
        image.logical_size = {static_cast<float>(logical_width),
                              static_cast<float>(logical_height)};
        image.source_scale = positiveScale(target_scale);
        image.resource = 0U;
        image.loaded = true;
        image.owned_resource = false;
        return true;
    } catch (...) {
        // DのcreateImageDataはラスタ画像の例外を捕捉し、空の画像を返す。
        return false;
    }
}

void composite(lib::ImageFrame& destination, const lib::ImageFrame& source,
               const PileImage::Append& layer, const std::uint32_t target_scale) {
    if (destination.pixel_width == 0U || destination.pixel_height == 0U ||
        source.pixel_width == 0U || source.pixel_height == 0U) return;
    // D版と同じ画像全体の中央・入力倍率・余白の計算を、画像ペインと共有する。
    const auto placement=lib::plan_image_layer_destination({destination.pixel_width,destination.pixel_height,
        source.pixel_width,source.pixel_height,target_scale,positiveScale(layer.image.source_scale),
        {layer.insets.n,layer.insets.e,layer.insets.s,layer.insets.w},layer.scale_type});
    const int draw_x=placement.x,draw_y=placement.y,draw_width=placement.width,draw_height=placement.height;
    const auto mask = pixelAt(source, 0U, 0U);
    for (int dy = 0; dy < draw_height; ++dy) {
        const int yy = draw_y + dy;
        if (yy < 0 || yy >= static_cast<int>(destination.pixel_height)) continue;
        const auto source_y = std::min(source.pixel_height - 1U,
            static_cast<std::uint32_t>(static_cast<std::uint64_t>(dy) *
                                       source.pixel_height /
                                       static_cast<std::uint32_t>(std::max(1, draw_height))));
        for (int dx = 0; dx < draw_width; ++dx) {
            const int xx = draw_x + dx;
            if (xx < 0 || xx >= static_cast<int>(destination.pixel_width)) continue;
            const auto source_x = std::min(source.pixel_width - 1U,
                static_cast<std::uint32_t>(static_cast<std::uint64_t>(dx) *
                                           source.pixel_width /
                                           static_cast<std::uint32_t>(std::max(1, draw_width))));
            auto foreground = pixelAt(source, source_x, source_y);
            if (layer.transparent && foreground.red == mask.red &&
                foreground.green == mask.green && foreground.blue == mask.blue) continue;
            foreground.alpha = static_cast<std::uint8_t>(
                static_cast<std::uint32_t>(foreground.alpha) * layer.alpha / 255U);
            const auto background = pixelAt(destination,
                                            static_cast<std::uint32_t>(xx),
                                            static_cast<std::uint32_t>(yy));
            const auto alpha = static_cast<std::uint32_t>(foreground.alpha);
            lib::Color blended;
            blended.red = static_cast<std::uint8_t>((foreground.red * alpha +
                background.red * (255U - alpha)) / 255U);
            blended.green = static_cast<std::uint8_t>((foreground.green * alpha +
                background.green * (255U - alpha)) / 255U);
            blended.blue = static_cast<std::uint8_t>((foreground.blue * alpha +
                background.blue * (255U - alpha)) / 255U);
            blended.alpha = static_cast<std::uint8_t>(alpha +
                background.alpha * (255U - alpha) / 255U);
            putPixel(destination, static_cast<std::uint32_t>(xx),
                     static_cast<std::uint32_t>(yy), blended);
        }
    }
}

}  // namespace

namespace detail {

std::uint8_t cardNameAverage(const lib::ImageData& data,const structs::CRect area) {
    return dutils::getRGBAverage(data,area);
}

lib::Point toLibPoint(const structs::CPoint value) noexcept {
    return {static_cast<float>(value.x), static_cast<float>(value.y)};
}

ImageDataWithScale detailLoadImage(Props& prop, const Skin& skin,
                                   const Summary* summary, const StringView path,
                                   const std::uint32_t target_scale,
                                   const lib::Size fallback) {
    return detailLoadImage(prop, skin, summary, path, target_scale, fallback, false);
}

ImageDataWithScale detailLoadImage(Props& prop, const Skin& skin,
                                   const Summary* summary, const StringView path,
                                   const std::uint32_t target_scale,
                                   const lib::Size fallback,
                                   const bool transparent,
                                   const bool publish_resource,
                                   const bool is_image_cell) {
    const auto source_scale = positiveScale(target_scale);
    if (path.empty()) return fallbackData(fallback, source_scale);

    // DのImageSelectは、CardWirthの透過色マスクを
    // 有効にしてdutils.loadImageWithScaleを使
    // う。この経路は左上画素のマスクに加え、二種類の不正なCWNext
    // BMPの修復と、拡大画像・シナリオ・スキンの素材パスの解決を行
    // う。プレビューも同じ経路に保つ。下記の汎用コーデック経路は、画像
    // セル以外の不透明な資源用に残し、既存のキャッシュを維持する。背
    // 景画像セルはMaskが無効でもDのパレットキー規則を維持する。
    if (transparent || is_image_cell) {
        auto loaded = dutils::loadImageWithScale(
            &prop, &skin, summary, path, source_scale, transparent, is_image_cell, 0, 0, {},
            prop.images().services());
        ImageDataWithScale result;
        result.source_scale = positiveScale(loaded.scale());
        if (loaded.valid()) {
            result.data = std::move(loaded.baseData());
            assignLogicalSize(result);
            result.loaded = true;
        }
        if (result.logical_size.width <= 0.0F || result.logical_size.height <= 0.0F)
            result.logical_size = fallback;
        return result;
    }

    const bool binary = utils::isBinImg(path);
    String resolved;
    if (!binary) {
        resolved = skin.findImagePath(path, scenarioPath(summary), dataVersion(summary));
        if (resolved.empty()) {
            std::error_code error;
            if (std::filesystem::is_regular_file(structs::pathFromUtf8(path), error)) {
                resolved = String(path);
            }
        }
    }

    ImageDataWithScale result;
    // この分岐は.x2/.x4を選ばず、解決したファイルを直接復
    // 号する。そのため高い描画倍率を要求されても画素は1倍。target_scale
    // を入力倍率と扱うと、Mask無効時に同じImageSelect
    // のプレビューが半分のサイズになる。
    result.source_scale = 1U;
    const auto& services = prop.images().services();
    if (services.image_codecs) {
        try {
            lib::DecodeImageRequest request;
            std::vector<std::uint8_t> encoded;
            if (binary) {
                encoded = utils::strToBImg(path);
                request.encoded_data = std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(encoded.data()), encoded.size());
            } else if (!resolved.empty()) {
                request.path = structs::pathFromUtf8(resolved);
            }
            if (binary || !resolved.empty()) {
                String error;
                result.data = services.image_codecs->decode(request, error);
            }
        } catch (...) {
            result.data = {};
        }
    }
    if (!result.data.frames.empty()) {
        assignLogicalSize(result);
        result.loaded = true;
    }
    // 多くの呼び出し側は、最終アップロードを所有するPileImage/FlexImage
    // などのプレビューへ画素を渡す。ここでの登録は明
    // 示的な選択とし、同じ入力をここで復号・アップロ
    // ードしてから合成後に再度行う二重処理を避ける。
    if (publish_resource && !binary && !resolved.empty()) {
        result.resource = prop.images().load(structs::pathFromUtf8(resolved));
    }
    if (result.logical_size.width <= 0.0F || result.logical_size.height <= 0.0F) {
        result.logical_size = fallback;
    }
    return result;
}

ImageDataWithScale detailCastCardImage(Props& prop, const Skin& skin,
                                       const Summary* summary,
                                       const EnemyCard* enemy,
                                       const CastCard* card, const bool debug_mode) {
    if (card != nullptr) {
        const auto overrideName = enemy != nullptr && enemy->isOverrideName();
        const auto overrideImage = enemy != nullptr && enemy->isOverrideImage();
        auto rendered = dutils::castCardImage(
            &prop, &skin, summary, card, debug_mode, overrideName,
            overrideName ? messageutils::createSPCharPreview(prop,summary,
                Skin::Ptr(const_cast<Skin*>(&skin),[](Skin*){}),enemy->useCounter().get(),enemy->overrideName(),false) : String{}, overrideImage,
            overrideImage ? enemy->overrideImages() : CardImageArray{},
            prop.images().services());
        ImageDataWithScale result;
        result.data = std::move(rendered.baseData());
        result.source_scale = rendered.scale();
        result.logical_size = imageSize(result.data);
        result.loaded = result.valid();
        return result;
    }
    // DのCast欠落時の経路は、全ホストでdskin::castCardを呼ぶ。Classic
    // のCARD_LARGE、拡大素材、ALLビルドで一つの資源優先順位を使う。
    auto source = dskin::castCard(skin, prop.drawingScale(), prop.images().services());
    if (source.valid()) {
        ImageDataWithScale result;
        result.data = std::move(source.data);
        result.resource = source.resource;
        result.logical_size = source.logical_size;
        result.source_scale = source.source_scale;
        result.loaded = source.loaded;
        result.owned_resource = source.owned_resource;
        return result;
    }
    // Dの共通検索が空の場合、C++のサイズ・素材による代替を維持する。
    skin::MaskType mask = skin::MaskType::NoMask;
    const auto path = skin.resCastCard(mask);
    const auto size = prop.looks().cardSize();
    return detailLoadImage(prop, skin, summary, path, prop.drawingScale(),
                           {static_cast<float>(size.width), static_cast<float>(size.height)});
}

ImageDataWithScale detailMenuCardImage(Props& prop, const Skin& skin,
                                       const Summary* summary) {
    // Dはメニューカード枠を通常のシナリオ素材として読み込まない。dskin::menuCard
    // はClassicのEngineのCARD_NORMAL資源も
    // 確認し、スキン指定のマスクを適用する。ここでdetailLoadImage
    // を使うと、特にClassic・外部シナリオでスキンパ
    // スを復号できない場合、別描画のタイトルだけが残る。
    const auto copySource = [](auto source) {
        ImageDataWithScale value;
        value.data = std::move(source.data);
        value.resource = source.resource;
        value.logical_size = source.logical_size;
        value.source_scale = source.source_scale;
        value.loaded = source.loaded;
        value.owned_resource = source.owned_resource;
        return value;
    };
    auto result = copySource(dskin::menuCard(skin, prop.drawingScale()));
    const auto size = prop.looks().cardSize();
    // Editorに設定されたサービス経由だけで資源を提供する非Classic
    // スキン用に、解決済みのスキンパスを代替として残す。
    if (result.data.frames.empty()) {
        skin::MaskType mask = skin::MaskType::NoMask;
        result = detailLoadImage(prop, skin, summary, skin.resMenuCard(mask),
                                 prop.drawingScale(),
                                 {static_cast<float>(size.width),
                                  static_cast<float>(size.height)});
    }
    // 外部・旧形式のシナリオは、スキン情報が不完全でも有効。D
    // はその場合もEditorの既定スキンでメニューカードを描
    // く。論理的な代替サイズは画像データではなく、ここで受け
    // 入れるとSceneViewに空の黒い矩形が出てしまう。
    if (result.data.frames.empty()) {
        const auto default_skin = dskin::findSkin2(
            prop, prop.var().defaultSkin.value, prop.var().defaultSkinName.value);
        if (default_skin && default_skin.get() != &skin) {
            result = copySource(dskin::menuCard(*default_skin,
                                                prop.drawingScale()));
            if (result.data.frames.empty()) {
                skin::MaskType mask = skin::MaskType::NoMask;
                result = detailLoadImage(prop, *default_skin, summary,
                                         default_skin->resMenuCard(mask),
                                         prop.drawingScale(),
                                         {static_cast<float>(size.width),
                                          static_cast<float>(size.height)});
            }
        }
    }
    if (result.logical_size.width <= 0.0F || result.logical_size.height <= 0.0F)
        result.logical_size = {static_cast<float>(size.width),
                               static_cast<float>(size.height)};
    return result;
}

ImageDataWithScale detailTalkerImage(Props& prop, const Skin& skin,
                                     const Summary* summary,
                                     const types::Talker talker) {
    if (talker == types::Talker::Card)
        return detailMenuCardImage(prop, skin, summary);
    String file;
    switch (talker) {
    case types::Talker::Selected: file = "talker_sel.png"; break;
    case types::Talker::Unselected: file = "talker_unsel.png"; break;
    case types::Talker::Random: file = "talker_random.png"; break;
    case types::Talker::Valued: file = "talker_valued.png"; break;
    case types::Talker::Card: break;
    }
    if (file.empty()) return {};
    const auto path = structs::pathFromUtf8(prop.images().appPath()).parent_path() /
                      structs::pathFromUtf8("resource") /
                      structs::pathFromUtf8(file);
    return detailLoadImage(prop, skin, summary, structs::pathToUtf8(path),
                           prop.drawingScale());
}

void appendTalkerCardImage(PileImage& target, const CardImage& image,
                           Props& prop, const Skin& skin,
                           const Summary* summary) {
    if (image.type != types::CardImageType::Talker ||
        image.talker() != types::Talker::Card) return;
    const auto scale=positiveScale(prop.drawingScale());
    const auto logical_width=std::max(1,target.baseWidth());
    const auto logical_height=std::max(1,target.baseHeight());
    const auto width=static_cast<std::int32_t>(std::min<std::uint32_t>(
        scaledSceneDimension(logical_width,scale),
        static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())));
    const auto height=static_cast<std::int32_t>(std::min<std::uint32_t>(
        scaledSceneDimension(logical_height,scale),
        static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())));
    dutils::PileImage common_pile(dutils::ImageDataWithScale(
        dutils::blankImage(width,height),scale));
    // 公開されたD移植の補助処理を再利用し、Talker.Cardのカードサ
    // イズ画像、余白0の配置、Cutの動作、アルファ規則を一か所で管理する。
    dutils::addToPileImage(image,common_pile,&prop,&skin,summary,
        prop.looks().menuCardInsets(),lib::ImageScaleMode::None,
        prop.images().services());
    if(common_pile.layers.empty())return;
    auto& shared=common_pile.layers.front();
    ImageDataWithScale layer(std::move(shared.baseData()),shared.scale());
    layer.logical_size={static_cast<float>(logical_width),
                        static_cast<float>(logical_height)};
    target.append(std::move(layer),structs::CInsets{},ScaleType::Cut,
                  255U,false,scale);
}

ImageDataWithScale detailPcNumberImage(Props& prop, const std::uint32_t number,
                                       const bool legacy) {
    if (!lib::editor_window_host_options().all_mode) {
        // Dは設定されたPC番号サイズの半分でFontDataを追加し、メニュ
        // ーカードの内側余白内で中央配置する。単位は画素高ではなくポイント。
        auto configured = prop.adjustFont(prop.looks().pcNumberFont(legacy));
        configured.point /= 2;
        const auto scale = positiveScale(prop.drawingScale());
        lib::ImageTextStyle style;
        style.font.family = configured.name;
        style.font.points = static_cast<float>(configured.point) * scale;
        style.font.weight = configured.bold ? 700 : 400;
        style.font.italic = configured.italic;
        style.preserve_font_antialias = true;
        auto frame = lib::rasterize_image_text(prop.images().services().graphics,
                                              std::to_string(number), style);
        if (!frame.pixel_width || !frame.pixel_height) return {};
        lib::ImageData data;
        data.format = lib::ImageFormat::Bitmap;
        data.frames.push_back(std::move(frame));
        return ImageDataWithScale(std::move(data), scale);
    }
    static constexpr std::array<std::uint8_t, 10> rows[7] = {
        {0x0e,0x04,0x0e,0x0e,0x0a,0x0f,0x06,0x0f,0x0e,0x0e},
        {0x11,0x0c,0x11,0x11,0x0a,0x10,0x08,0x01,0x11,0x11},
        {0x13,0x04,0x01,0x01,0x0a,0x10,0x10,0x02,0x11,0x11},
        {0x15,0x04,0x02,0x06,0x0f,0x1e,0x1e,0x04,0x0e,0x0f},
        {0x19,0x04,0x04,0x01,0x02,0x01,0x11,0x08,0x11,0x01},
        {0x11,0x04,0x08,0x11,0x02,0x11,0x11,0x08,0x11,0x02},
        {0x0e,0x0e,0x1f,0x0e,0x02,0x0e,0x0e,0x08,0x0e,0x0c}
    };
    const auto text = std::to_string(number);
    const auto pixel_scale = std::max<std::uint32_t>(1U, prop.drawingScale());
    lib::ImageFrame frame;
    frame.pixel_width = static_cast<std::uint32_t>(text.size() * 6U) * pixel_scale;
    frame.pixel_height = 7U * pixel_scale;
    frame.logical_size = {static_cast<float>(text.size() * 6U), 7.0F};
    frame.pixel_format = lib::PixelFormat::BGRA32;
    frame.pixels.resize(static_cast<std::size_t>(frame.pixel_width) *
                        frame.pixel_height * 4U);
    for (std::size_t digit_index = 0; digit_index < text.size(); ++digit_index) {
        const auto digit = static_cast<std::size_t>(text[digit_index] - '0');
        for (std::uint32_t y = 0; y < 7U; ++y) {
            for (std::uint32_t x = 0; x < 5U; ++x) {
                if ((rows[y][digit] & (1U << (4U - x))) == 0U) continue;
                for (std::uint32_t yy = 0; yy < pixel_scale; ++yy)
                    for (std::uint32_t xx = 0; xx < pixel_scale; ++xx)
                        putPixel(frame,
                                 (static_cast<std::uint32_t>(digit_index) * 6U + x) *
                                     pixel_scale + xx,
                                 y * pixel_scale + yy, {255, 255, 255, 255});
            }
        }
    }
    lib::ImageData data;
    data.format = lib::ImageFormat::Bitmap;
    data.frames.push_back(std::move(frame));
    return ImageDataWithScale(std::move(data), pixel_scale);
}

}  // namespace detail

ImageDataWithScale::ImageDataWithScale(lib::ImageData value,
                                       const std::uint32_t scale)
    : data(std::move(value)), source_scale(positiveScale(scale)), loaded(!data.frames.empty()) {
    assignLogicalSize(*this);
}

bool ImageDataWithScale::valid() const noexcept {
    return resource != 0 || !data.frames.empty() ||
           (logical_size.width > 0.0F && logical_size.height > 0.0F);
}

lib::Size ImageDataWithScale::size() const noexcept {
    if (logical_size.width > 0.0F && logical_size.height > 0.0F) return logical_size;
    const auto size = imageSize(data);
    const auto scale = static_cast<float>(positiveScale(source_scale));
    return {size.width / scale, size.height / scale};
}

void ImageDataWithScale::clear() noexcept {
    data = {};
    resource = 0;
    logical_size = {};
    source_scale = 1U;
    loaded = false;
    owned_resource = false;
}

PileImage::PileImage(String path, const std::uint32_t target_scale,
                     const std::int32_t x, const std::int32_t y,
                     const std::int32_t base_width,
                     const std::int32_t base_height, const bool scale_mode,
                     const bool data_resizable)
    : path_(std::move(path)), target_scale_(positiveScale(target_scale)),
      x_(x), y_(y), base_width_(base_width), base_height_(base_height),
      width_(base_width), height_(base_height), scale_mode_(scale_mode),
      data_resizable_(data_resizable) {}

PileImage::PileImage(ImageDataWithScale image, const std::uint32_t target_scale,
                     const std::int32_t x, const std::int32_t y,
                     const std::int32_t base_width,
                     const std::int32_t base_height, const bool scale_mode,
                     const bool data_resizable)
    : image_(std::move(image)), target_scale_(positiveScale(target_scale)),
      x_(x), y_(y), base_width_(base_width), base_height_(base_height),
      width_(base_width), height_(base_height), scale_mode_(scale_mode),
      data_resizable_(data_resizable) {
    base_image_ = image_;
    base_image_.owned_resource = false;
}

PileImage::~PileImage() { lifetime_.reset();releaseResource(); }

PileImage::PileImage(PileImage&& other) noexcept
    : path_(std::move(other.path_)), image_(std::move(other.image_)),
      base_image_(std::move(other.base_image_)), appends_(std::move(other.appends_)),
      target_scale_(other.target_scale_), scale_(other.scale_), x_(other.x_), y_(other.y_),
      base_width_(other.base_width_), base_height_(other.base_height_), width_(other.width_),
      height_(other.height_), layer_(other.layer_), scale_mode_(other.scale_mode_),
      data_resizable_(other.data_resizable_), transparent_(other.transparent_),
      color_key_prepared_(other.color_key_prepared_),
      smoothing_(other.smoothing_), alpha_(other.alpha_), separator_(other.separator_),
      visible_(other.visible_), created_(other.created_), graphics_(std::move(other.graphics_)),
      source_services_(std::move(other.source_services_)),
      title_assigned_(other.title_assigned_), title_(std::move(other.title_)), title_font_(other.title_font_),
      title_point_(other.title_point_),
      title_shrink_width_(other.title_shrink_width_),
      title_antialias_(other.title_antialias_),title_color_(other.title_color_),title_hemming_color_(other.title_hemming_color_) {
    other.image_.owned_resource = false;
    other.image_.resource = 0;
    other.base_image_.resource = 0;
    ++other.image_revision_;
    other.created_ = false;
}

PileImage& PileImage::operator=(PileImage&& other) noexcept {
    if (this == &other) return *this;
    const auto keep_alive = weak_from_this().lock();
    const auto old_graphics = graphics_;
    const auto old_id = image_.owned_resource ? image_.resource : 0;
    ++image_revision_;
    path_ = std::move(other.path_);
    image_ = std::move(other.image_);
    base_image_ = std::move(other.base_image_);
    appends_ = std::move(other.appends_);
    target_scale_ = other.target_scale_; scale_ = other.scale_;
    x_ = other.x_; y_ = other.y_; base_width_ = other.base_width_; base_height_ = other.base_height_;
    width_ = other.width_; height_ = other.height_; layer_ = other.layer_;
    scale_mode_ = other.scale_mode_; data_resizable_ = other.data_resizable_;
    transparent_ = other.transparent_; smoothing_ = other.smoothing_; alpha_ = other.alpha_;
    color_key_prepared_=other.color_key_prepared_;
    separator_ = other.separator_; visible_ = other.visible_; created_ = other.created_;
    title_assigned_ = other.title_assigned_;
    graphics_ = std::move(other.graphics_); title_ = std::move(other.title_);
    source_services_=std::move(other.source_services_);
    title_font_ = other.title_font_; title_point_ = other.title_point_;
    title_shrink_width_ = other.title_shrink_width_;
    title_antialias_ = other.title_antialias_;
    title_color_=other.title_color_;title_hemming_color_=other.title_hemming_color_;
    other.image_.owned_resource = false; other.image_.resource = 0; other.created_ = false;
    other.base_image_.resource = 0;
    ++other.image_revision_;
    // 旧画像を解放する前に移動を反映する。解放コールバック
    // が新しい画像操作を開始した場合、後から上書きしない。
    if (old_graphics && old_id) old_graphics->destroy_image(old_id);
    return *this;
}

void PileImage::releaseResource() noexcept {
    const auto graphics = graphics_;
    const auto id = image_.owned_resource ? image_.resource : 0;
    image_.resource = 0;
    image_.owned_resource = false;
    if (id && base_image_.resource == id) base_image_.resource = 0;
    created_ = false;
    if (graphics && id) graphics->destroy_image(id);
}

bool PileImage::updatePath(const StringView old_path, const StringView new_path) {
    // Dの使用中の場面・プレビュー画像は、ファイル名変更をまた
    // いで同一性を保つ。内容は変わらないため復号画素は保持し、
    // 保存した全入力パスを更新して合成済み表示を無効にする。
    const String from(old_path), to(new_path);
    bool changed = false;
    if (!path_.empty() && utils::cfnmatch(path_, from)) {
        path_ = to;
        changed = true;
    }
    for (auto& layer : appends_) {
        if (!layer.path.empty() && utils::cfnmatch(layer.path, from)) {
            layer.path = to;
            changed = true;
        }
    }
    if (changed) invalidateImage();
    return changed;
}

void PileImage::invalidateImage() noexcept {
    ++image_revision_;
    created_ = false;
}

void PileImage::setGraphics(lib::Owner<lib::GraphicsBackend> graphics) noexcept {
    if (graphics_ == graphics) return;
    const auto keep_alive = weak_from_this().lock();
    const auto previous = std::move(graphics_);
    const auto id = image_.owned_resource ? image_.resource : 0;
    graphics_ = std::move(graphics);
    ++image_revision_;
    created_ = false;
    if (previous) {
        image_.resource = base_image_.resource = 0;
        image_.owned_resource = false;
        if (id) previous->destroy_image(id);
    }
}

void PileImage::targetScale(const std::uint32_t value) noexcept {
    target_scale_ = positiveScale(value);
}

void PileImage::scale(const std::uint32_t value) noexcept {
    scale_ = std::max<std::uint32_t>(1U, value);
    resize();
}

lib::Rect PileImage::bounds() const noexcept {
    return {static_cast<float>(x_), static_cast<float>(y_),
            static_cast<float>(width_), static_cast<float>(height_)};
}

void PileImage::bounds(const lib::Rect value) noexcept {
    x_ = static_cast<std::int32_t>(std::lround(value.x));
    y_ = static_cast<std::int32_t>(std::lround(value.y));
    width_ = clampDimension(value.width);
    height_ = clampDimension(value.height);
}

void PileImage::alpha(const std::int32_t value) noexcept {
    const auto next=std::clamp(value,0,255);
    if(alpha_==next)return;
    alpha_=next;invalidateImage();
}

void PileImage::transparent(const bool value) noexcept {
    if (transparent_ == value) return;
    transparent_ = value;
    invalidateImage();
}

void PileImage::append(String path, const structs::CInsets insets,
                       const ScaleType scale_type, const bool transparent,
                       const std::int32_t mask_x, const std::int32_t mask_y,
                       const std::uint8_t alpha, const std::uint32_t drawing_scale) {
    Append value;
    value.path = std::move(path);
    value.insets = insets;
    value.point = {mask_x, mask_y};
    value.scale_type = scale_type;
    value.alpha = alpha;
    value.transparent = transparent;
    value.drawing_scale = positiveScale(drawing_scale);
    value.positioned = mask_x != 0 || mask_y != 0;
    appends_.push_back(std::move(value));
    invalidateImage();
}

void PileImage::append(ImageDataWithScale image, const structs::CInsets insets,
                       const ScaleType scale_type, const std::uint8_t alpha,
                       const bool transparent, const std::uint32_t drawing_scale) {
    append({}, std::move(image), insets, scale_type, alpha, transparent,
           drawing_scale);
}

void PileImage::append(String path, ImageDataWithScale image,
                       const structs::CInsets insets,
                       const ScaleType scale_type, const std::uint8_t alpha,
                       const bool transparent, const std::uint32_t drawing_scale) {
    Append value;
    value.path = std::move(path);
    value.image = std::move(image); value.insets = insets;
    value.scale_type = scale_type; value.alpha = alpha;
    value.transparent = transparent;
    value.drawing_scale = positiveScale(drawing_scale);
    appends_.push_back(std::move(value));
    invalidateImage();
}

void PileImage::append(ImageDataWithScale image, const structs::CPoint point,
                       const ScaleType scale_type, const std::uint8_t alpha) {
    Append value;
    const auto image_size = image.size();
    value.image = std::move(image);
    value.point = point;
    value.insets = {point.y,
                    base_width_ - point.x - clampDimension(image_size.width),
                    base_height_ - point.y - clampDimension(image_size.height),
                    point.x};
    value.scale_type = scale_type; value.alpha = alpha; value.positioned = true;
    appends_.push_back(std::move(value));
    invalidateImage();
}

void PileImage::setBaseImage(ImageDataWithScale image,
                             const std::int32_t base_width,
                             const std::int32_t base_height) {
    const auto keep_alive = weak_from_this().lock();
    auto base = image; // IDの公開・破棄の前に、すべてのメモリ確保を行う。
    base.owned_resource = false;
    const auto old_id = image_.owned_resource ? image_.resource : 0;
    // 置換画素の準備ができるまで公開中のアップロードを保持する。バックエンド
    // が同じなら、createImage()はこのIDをそのまま更新する。
    if (old_id) {
        image.resource = old_id;
        image.owned_resource = true;
    }
    ++image_revision_;
    created_ = false;
    image_ = std::move(image);
    base_image_ = std::move(base);
    base_width_ = base_width; base_height_ = base_height;
    width_ = base_width; height_ = base_height;
}

void PileImage::setTitle(String title, const structs::CFont font,
                         const lib::Point point, const std::int32_t shrink_width,
                         const bool antialias) {
    title_assigned_ = true;
    title_ = std::move(title); title_font_ = font; title_point_ = point;
    title_shrink_width_ = std::max(0, shrink_width);
    title_antialias_ = antialias;
    invalidateImage();
}

void PileImage::createImage() {
    if (created_ || creating_image_) return;
    const auto keep_alive = weak_from_this().lock();
    const std::weak_ptr<void> life=lifetime_;
    creating_image_ = true;
    struct CreatingGuard { bool* value;std::weak_ptr<void> life; ~CreatingGuard() { if(!life.expired())*value = false; } } guard{&creating_image_,life};
    const auto revision = image_revision_;
    const auto graphics = graphics_;
    const auto old_id = image_.owned_resource ? image_.resource : 0;

    // 常に装飾前の入力元から再構築する。WM_PAINT
    // ごとに追加レイヤーを再合成すると、場面の再描画
    // が遅くなり、半透明画素も徐々に変わってしまう。
    auto prepared = base_image_;
    prepared.owned_resource = false;
    // Dのimages.dは、空文字列を含む設定済みタイトルと追加画
    // 像に、不透明な白のmatNoTransparentを選ぶ。単体
    // 版とALLの場面合成で共有する。場面カードのファクトリーは透過
    // 色モードを明示的に無効化するが、他の呼び出し側のキー動作は維持
    // する。復号後がRGBAでも、Dの入力元の色深度は判断できない。
    const bool flatten_base = !transparent_ && (title_assigned_ || !appends_.empty());
    const bool needs_pixels = flatten_base || !appends_.empty() || !title_.empty() || (transparent_ && !color_key_prepared_);
    // スキンの破損・欠落で基底枠を取得できなくても、重ねる画
    // 像は表示する。Dは先にカードサイズの合成面を作り、シナ
    // リオのカード画像を追加する。base_image_が論
    // 理的な代替サイズだけの場合も、追加をすべて省略しない。
    if (prepared.data.frames.empty() && (flatten_base || !appends_.empty() || !title_.empty()) &&
        prepared.logical_size.width > 0.0F &&
        prepared.logical_size.height > 0.0F) {
        lib::ImageFrame canvas;
        canvas.logical_size = prepared.logical_size;
        const auto scale = positiveScale(prepared.source_scale);
        canvas.pixel_width = static_cast<std::uint32_t>(std::max(
            1, clampDimension(prepared.logical_size.width * scale)));
        canvas.pixel_height = static_cast<std::uint32_t>(std::max(
            1, clampDimension(prepared.logical_size.height * scale)));
        canvas.pixel_format = lib::PixelFormat::BGRA32;
        canvas.pixels.resize(static_cast<std::size_t>(canvas.pixel_width) *
                             canvas.pixel_height * 4U);
        prepared.data.frames = {std::move(canvas)};
        prepared.data.format = lib::ImageFormat::Bitmap;
    }
    bool raster_failed = false;
    if (data_resizable_ && base_width_ > 0 && base_height_ > 0 &&
        !prepared.data.frames.empty()) {
        // DのgetMatは、追加画像やタイトルの合成前に、初
        // 期サイズを対象倍率にした面を提供する。その合成キャン
        // バスを、代替スキン資源の元の解像度から独立させる。
        if (!resizeResizableBase(prepared,base_width_,base_height_,target_scale_,smoothing_)) {
            const auto width=std::min<std::uint32_t>(
                scaledSceneDimension(base_width_,target_scale_),
                static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()));
            const auto height=std::min<std::uint32_t>(
                scaledSceneDimension(base_height_,target_scale_),
                static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()));
            try {
                ImageDataWithScale blank(dutils::blankImage(static_cast<int>(width),
                    static_cast<int>(height)),positiveScale(target_scale_));
                blank.logical_size={static_cast<float>(base_width_),
                                    static_cast<float>(base_height_)};
                for(auto& frame:blank.data.frames)
                    frame.logical_size=blank.logical_size;
                blank.data.format=lib::ImageFormat::Bitmap;
                prepared=std::move(blank);
                raster_failed=true;
            } catch (...) {
                // 空の代替画像すら確保できない場合は、以前の公開画像を保持する。
                return;
            }
        }
    }
    if (!raster_failed && !prepared.data.frames.empty() && needs_pixels) {
        auto composed = rgbaFrame(prepared.data.frames.front());
        if (flatten_base) {
            for (std::size_t i = 0; i + 3 < composed.pixels.size(); i += 4) {
                const unsigned alpha = std::to_integer<unsigned>(composed.pixels[i + 3]);
                for (std::size_t c = 0; c != 3; ++c) {
                    const unsigned value = std::to_integer<unsigned>(composed.pixels[i + c]);
                    composed.pixels[i + c] = static_cast<std::byte>(
                        (value * alpha + 255U * (255U - alpha) + 127U) / 255U);
                }
                composed.pixels[i + 3] = std::byte{255};
            }
        }
        for (auto& layer : appends_) {
            if (!layer.image.data.frames.empty())
                composite(composed, layer.image.data.frames.front(), layer, positiveScale(prepared.source_scale));
        }
        if (transparent_ && !color_key_prepared_) applyTopLeftColorKey(composed);
        if(graphics && !title_.empty()) {
            const auto scale=positiveScale(prepared.source_scale);
            auto font=dutils::createFontFromPixels(title_font_,false);font.points*=scale;
            const auto& outline=title_hemming_color_;
            const lib::Point position{title_point_.x*scale-1,title_point_.y*scale-1};
            auto caption=lib::rasterize_image_text(graphics,title_,{font,title_color_,
                {static_cast<lib::Byte>(outline.r),static_cast<lib::Byte>(outline.g),static_cast<lib::Byte>(outline.b),static_cast<lib::Byte>(outline.a)},float(title_shrink_width_)*scale,title_antialias_});
            if(life.expired() || revision!=image_revision_ || graphics!=graphics_)return;
            if(caption.pixel_width && caption.pixel_height)
                (void)lib::composite_image_pixels(composed,caption,
                    position,
                    {0,0,float(composed.pixel_width),float(composed.pixel_height)});
        }
        prepared.data.frames = {std::move(composed)};
        prepared.data.format = lib::ImageFormat::Bitmap;
        prepared.resource = 0;
        prepared.loaded = true;
    }
    // Dは描画時にGCのアルファ値を渡す。共通Canvas APIでは、変更しな
    // い基底データから再構築した、この画像所有のアップロードに不透明度を含める。
    lib::apply_image_opacity(prepared.data,static_cast<lib::Byte>(alpha_));
    if (graphics && !prepared.data.frames.empty()) {
        bool created_resource = false;
        if (old_id != 0U) {
            // 通常の場面変更では資源IDを維持する。破棄・作成
            // の間に空白がちらつくのを避け、ネイティブとSDL
            // のバックエンドで画素だけを置換できるようにする。
            graphics->update_image(old_id, prepared.data);
            prepared.resource = old_id;
            prepared.owned_resource = true;
        } else {
            // 入力元はキャッシュから借用したResourceIdではなく復号済み画素。キ
            // ャッシュのリセットで無効にならないよう、独立したアップロードを保持する。
            prepared.resource = graphics->create_image(prepared.data);
            prepared.owned_resource = prepared.resource != 0;
            created_resource = prepared.owned_resource;
        }
        if (life.expired() || revision != image_revision_ || graphics != graphics_) {
            if (created_resource) graphics->destroy_image(prepared.resource);
            return;
        }
        if (!prepared.resource) throw std::runtime_error("Unable to prepare editor scene image");
    }
    if (old_id && old_id == prepared.resource) prepared.owned_resource = true;
    image_ = std::move(prepared);
    created_ = image_.resource != 0 || image_.valid();
    // 所有するアップロードIDを不変の基底へコピーしない。解除後に
    // 、無効な非所有のキャッシュIDとして再利用されてしまうため。
    if (graphics && old_id && old_id != image_.resource) graphics->destroy_image(old_id);
}

void PileImage::clearImage() noexcept {
    const auto keep_alive = weak_from_this().lock();
    ++image_revision_;
    releaseResource();
}

void PileImage::resize() {
    if (base_width_ <= 0 || base_height_ <= 0) {
        const auto size = image_.size();
        if (base_width_ <= 0) base_width_ = clampDimension(size.width);
        if (base_height_ <= 0) base_height_ = clampDimension(size.height);
    }
    const auto scale = static_cast<std::int64_t>(std::max<std::uint32_t>(1U, scale_));
    const auto scaled_dimension = [scale](const std::int32_t base) {
        // Dの整数式は、指定の百分率を掛けた後で切り捨てる。lround
        // を使うと奇数のカード寸法の75%が1画素大きくな
        // り、倍率編集を繰り返すとモデルからずれていく。
        const auto value = static_cast<std::int64_t>(std::max(1, base)) * scale / 100;
        return static_cast<std::int32_t>(std::clamp<std::int64_t>(
            value, 1, std::numeric_limits<std::int32_t>::max()));
    };
    width_ = scaled_dimension(base_width_);
    height_ = scaled_dimension(base_height_);
}

void FlexImage::resize() {
    const auto min_scale = std::max<std::uint32_t>(1U, minimumScale);
    const auto max_scale = std::max(min_scale, maximumScale);
    newScale = std::clamp(newScale, min_scale, max_scale);
    scale_ = newScale;
    if (scale_mode_) {
        PileImage::resize();
    } else {
        // DのFlexImageは、どちらかの軸が0でも明示的な矩形を確定する。
        width_ = std::max<std::int32_t>(0, newWidth);
        height_ = std::max<std::int32_t>(0, newHeight);
    }
    createImage();
}


lib::Owner<PileImage> createCastCardBackImage(
    Props& prop, Skin& skin, const Summary* summary, const std::int32_t x,
    const std::int32_t y, const std::uint8_t alpha) {
    const auto card_size = prop.looks().cardSize();
    const auto padding = prop.looks().castCardInsets();
    const auto width = card_size.width + padding.e + padding.w;
    const auto height = card_size.height + padding.n + padding.s;
    (void)summary;
    auto source=dskin::castCard(skin,prop.drawingScale(),prop.images().services());
    // Dはパーティカードのアルファ面を一定値で上書きする。不変の基底を不透
    // 明に正規化してから、アップロード時に同じ一定のアルファ値を適用する。
    lib::set_image_alpha(source.data,255);
    ImageDataWithScale data(std::move(source.data),source.source_scale);
    auto result = std::make_shared<PileImage>(std::move(data), prop.drawingScale(), x, y,
                                               width, height, true, true);
    result->setGraphics(prop.images().services().graphics);
    // DのSkinは全体のGUIサービスを使う。設定済みの共通サービスの
    // 所有元を保持し、この共通画像のスキン置換でも同じデコーダーを使う。
    result->sourceServices(prop.images().services());
    result->alpha(alpha);
    result->layer(types::LAYER_PLAYER_CARD * 10);
    result->createImage();
    return result;
}

lib::Owner<FlexImage> createBackgroundImage(
    Props& prop, const Skin& skin, const Summary* summary, const StringView path,
    const std::int32_t x, const std::int32_t y, const std::int32_t width,
    const std::int32_t height, const bool transparent, const std::int32_t layer,
    const bool smoothing) {
    auto extension = structs::pathFromUtf8(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char value) {
                       return static_cast<char>(std::tolower(value));
                   });
    const bool is_jpy = extension == ".jpy1" || extension == ".jptx" || extension == ".jpdc";
    const auto fallback = lib::Size{static_cast<float>(width), static_cast<float>(height)};
    bool resizable=true;
    ImageDataWithScale data;
    if(is_jpy) {
        // Dのエフェクトブースター出力は、自身の拡大縮小可否を提供す
        // る。拡張子だけでは生成画素を拡大縮小するか決められない。
        data=jpyimage::loadJPYImageWithScale(prop,&skin,summary,String(path),prop.drawingScale(),{},resizable);
    } else {
        resizable=utils::isBinImg(path);
        // 同じDの画像セル読み込みで、不透明・マスク付き素
        // 材、不正BMP修復、GIF透過、拡大画像を扱う。
        bool decoded{};
        auto loaded=dutils::loadImageWithScale(&prop,&skin,summary,path,prop.drawingScaleForImage(summary),transparent,true,0,0,{},prop.images().services(),&decoded);
        data=ImageDataWithScale(std::move(loaded.baseData()),loaded.scale());
        // Dは通常ファイルのサイズ取得に失敗した場合、指定済みの基底
        // 矩形を維持する。共通読み込みの透明な空画像は表示用の代替で
        // あり、欠落した入力が有効な1x1素材である証拠ではない。
        if(!resizable && !decoded)data.logical_size=fallback;
    }
    if(!data.valid())data.logical_size=fallback;
    const auto data_size = data.size();
    const auto base_width = data_size.width > 0.0F ? clampDimension(data_size.width) : width;
    const auto base_height = data_size.height > 0.0F ? clampDimension(data_size.height) : height;
    auto result = std::make_shared<FlexImage>(std::move(data), prop.drawingScale(), x, y,
                                               base_width, base_height, false, resizable);
    result->path(String(path));
    result->setGraphics(prop.images().services().graphics);
    result->transparent(transparent);
    // 共通の画像セル読み込みは、形式別のマスク規則を適用済み。
    // 無条件に二度目の透過色処理を行うと、Dが除外するJPEG/PNG
    // の色が消え、生成したJPY画素も変わってしまう。
    result->sourceColorKeyPrepared(true);
    result->layer(layer * 10);
    result->smoothing(smoothing);
    result->newWidth = width;
    result->newHeight = height;
    result->resize();
    return result;
}

void updateCastCardBackImage(PileImage& image, Skin& skin,
                             const std::uint8_t alpha,
                             const std::uint32_t target_scale) {
    auto source=dskin::castCard(skin,target_scale,image.sourceServices());
    lib::set_image_alpha(source.data,255);
    ImageDataWithScale replacement(std::move(source.data),source.source_scale);
    image.setBaseImage(std::move(replacement),image.baseWidth(),image.baseHeight());
    image.alpha(alpha);
    image.targetScale(target_scale);
    image.createImage();
}

lib::Owner<BgImagesView> createBgImagesViewAndMenu(
    Commons comm, Props& prop, const Summary* summary, const Skin& skin,
    UseCounterPtr counter, BgImageContainer& container, lib::Container& parent,
    AbstractArea* ref_target, const bool show_inherit_backs, const bool read_only) {
    auto view = std::make_shared<BgImagesView>(std::move(comm), prop, summary, skin,
        std::move(counter), container, &parent, ref_target, show_inherit_backs, read_only);
    if (!lib::editor_window_host_options().all_mode) {
        auto menu = view->createMenuBar();
        for (const auto& child : parent.children()) {
            auto placement = child->layout_item();
            ++placement.row;
            child->set_layout_item(placement);
        }
        menu->set_layout_item({.row=0U, .column=0U});
        parent.insert(0U, menu);
    }
    // ALLでは意図的に指定された部品配置を維持する。既存のポップアッ
    // プ・ツールバーは同じ共通の場面コマンドへ配送する。通常のコンスト
    // ラクターで製品用の共通ルートツリーは組み込み済み。同じ表示が編
    // 集、TCPD、プレビュー、メニュー、共通の取り消しを提供する。
    return view;
}

lib::Owner<PileImage> createMessageImage(const Commons& comm, Props& prop,
                                         const Summary* summary) {
    const auto rect = prop.looks().messageBounds();
    ImageDataWithScale data;
    if (comm.messageImage) data = comm.messageImage(summary);
    if (!data.valid()) {
        messageutils::MessageContext context;context.services=comm.services;context.props=&prop;
        const auto skin=comm.skin ? Skin::Ptr(const_cast<Skin*>(comm.skin),[](Skin*){}) : Skin::Ptr{};
        const std::array<String,1> selections={String{}};
        // 空の装飾には特殊文字がないため、DはSummaryを渡さ
        // ない。通常の呼び出し側が省略可能な上書きコールバックを渡
        // さなくても、実際の共通メッセージウィンドウ画像を描く。
        auto preview=messageutils::previewMessage(context,prop,nullptr,[skin]{return skin;},{},{},false,"",selections,{},{},{},{},{},false,false,false,false);
        if(const auto* pixels=preview.data()) {
            data=ImageDataWithScale(*pixels,preview.scale);
            // メッセージの補助処理は、倍率を正規化した
            // 論理サイズをフレームに保存済み。この受け
            // 渡しでも保持し、入力倍率で再度割らない。
            data.logical_size=preview.logicalSize();
        }
    }
    const auto size = data.size();
    auto result = std::make_shared<PileImage>(std::move(data), prop.drawingScale(),
                                              rect.x, rect.y,
                                              clampDimension(size.width),
                                              clampDimension(size.height), false, true);
    result->setGraphics(prop.images().services().graphics);
    result->layer(types::LAYER_MESSAGE * 10 - 1);
    result->alpha(prop.var().messageAlpha.value);
    result->separator(true);
    result->createImage();
    return result;
}

String createFlagName(const Commons& comm, const Summary* summary,
                      UseCounterPtr counter, const StringView path) {
    if (comm.prop == nullptr) return {};
    const auto& messages = comm.prop->requiredMsgs();
    if (path.empty()) return messages.areaViewStatusNoFlag.value;
    if (summary && flag::findVar<flag::Flag>(summary->flagDirRoot(), counter, path)) {
        return utils::tryFormat(messages.areaViewStatusWithFlag.value, path);
    }
    return utils::tryFormat(messages.noFlag.value, path);
}

String createAreaViewStatusLine(const Commons& comm, const Summary* summary,
                                const Skin& skin, const MenuCard& card) {
    if (comm.prop == nullptr) return {};
    const auto& messages = comm.prop->requiredMsgs();
    String image_description;
    for (const auto& path : card.paths()) {
        if (!path) continue;
        switch (path->type) {
        case types::CardImageType::File:
            if (path->path().empty()) {
                image_description += messages.noSelectImage.value;
            } else if (utils::isBinImg(path->path())) {
                image_description += messages.areaViewStatusImageIncluding.value;
            } else if (skin.findImagePath(path->path(), scenarioPath(summary),
                                          dataVersion(summary)).empty()) {
                image_description += utils::tryFormat(messages.noImage.value,
                                                       utils::encodePath(path->path()));
            } else {
                image_description += utils::encodePath(path->path());
            }
            image_description.push_back(' ');
            break;
        case types::CardImageType::PCNumber:
            if (path->pcNumber() > 0U) {
                image_description += utils::tryFormat(messages.pcNumber.value,
                                                       path->pcNumber());
                image_description.push_back(' ');
            }
            break;
        case types::CardImageType::Talker:
            image_description += messages.talkerName(path->talker());
            image_description.push_back(' ');
            break;
        }
    }
    if (image_description.empty()) image_description = messages.noSelectImage.value;
    else if (image_description.back() == ' ') image_description.pop_back();
    const auto params = statusParams(comm, summary, card);
    if (params.empty()) {
        return utils::tryFormat(messages.areaViewStatusNoSummary.value,
                                messages.menuCard.value, image_description);
    }
    return utils::tryFormat(messages.areaViewStatus.value, messages.menuCard.value,
                            image_description, params);
}

String createAreaViewStatusLine(const Commons& comm, const Summary* summary,
                                const Skin&, const EnemyCard& card) {
    if (comm.prop == nullptr) return {};
    const auto& messages = comm.prop->requiredMsgs();
    String name = messages.noSelectCast.value;
    if (card.id() != 0U) {
        if (summary) {
            if (const auto cast = summary->cwCast(card.id())) {
                name = utils::tryFormat(messages.areaViewStatusEnemyCard.value,
                                        cast->id(), cast->name());
            } else {
                name = utils::tryFormat(messages.noCast.value, card.id());
            }
        } else {
            name = utils::tryFormat(messages.noCast.value, card.id());
        }
    }
    const auto params = statusParams(comm, summary, card);
    if (params.empty()) {
        return utils::tryFormat(messages.areaViewStatusNoSummary.value,
                                messages.enemyCard.value, name);
    }
    return utils::tryFormat(messages.areaViewStatus.value, messages.enemyCard.value,
                            name, params);
}

String statusParams(const Commons& comm, const Summary* summary,
                    const AbstractSpCard& card) {
    if (comm.prop == nullptr) return {};
    const auto& messages = comm.prop->requiredMsgs();
    String result;
    if (summary) result = createFlagName(comm, summary, card.useCounter(), card.flag());
    if (card.layer() != types::LAYER_MENU_CARD) {
        if (!result.empty()) result.push_back(' ');
        result += utils::tryFormat(messages.areaViewStatusWithLayer.value, card.layer());
    }
    if (card.animationSpeed() != -1) {
        if (!result.empty()) result.push_back(' ');
        result += utils::tryFormat(messages.areaViewStatusWithCardSpeed.value,
                                   card.animationSpeed());
    }
    return result;
}

String createAreaViewStatusLine(const Commons& comm, const Summary* summary,
                                const Skin& skin, const BgImage& back) {
    if (comm.prop == nullptr) return {};
    const auto& messages = comm.prop->requiredMsgs();
    String name;
    String path;
    if (const auto* image = dynamic_cast<const background::ImageCell*>(&back)) {
        name = messages.back.value;
        path = utils::encodePath(image->path());
        if (path.empty()) {
            path = messages.noSelectImage.value;
        } else if (skin.findImagePath(image->path(), scenarioPath(summary),
                                      dataVersion(summary)).empty()) {
            path = utils::tryFormat(messages.noImage.value, path);
        }
    } else if (dynamic_cast<const background::TextCell*>(&back)) {
        name = messages.textCell.value;
        path = back.name(comm.prop->parent());
    } else if (dynamic_cast<const background::ColorCell*>(&back)) {
        name = messages.colorCell.value;
        path = back.name(comm.prop->parent());
    } else if (dynamic_cast<const background::PCCell*>(&back)) {
        name = messages.pcCell.value;
        path = back.name(comm.prop->parent());
    }
    const auto params = statusParams(comm, summary, back);
    if (params.empty()) {
        return utils::tryFormat(messages.areaViewStatusNoSummary.value, name, path);
    }
    return utils::tryFormat(messages.areaViewStatus.value, name, path, params);
}

String statusParams(const Commons& comm, const Summary* summary,
                    const BgImage& back) {
    if (comm.prop == nullptr) return {};
    String result;
    if (summary) result = createFlagName(comm, summary, back.useCounter(), back.flag());
    if (back.layer() != types::LAYER_BACK_CELL) {
        if (!result.empty()) result.push_back(' ');
        result += utils::tryFormat(comm.prop->requiredMsgs().areaViewStatusWithLayer.value,
                                   back.layer());
    }
    return result;
}

namespace {
[[nodiscard]] std::shared_ptr<const lib::WindowInputMask> previewInputMask(
    const lib::ImageData& image, const bool transparent) {
    if(image.frames.empty()) return {};
    const auto& frame=image.frames.front();
    const auto count=std::uint64_t(frame.pixel_width)*frame.pixel_height;
    if(!frame.pixel_width || !frame.pixel_height ||
       count>std::numeric_limits<std::size_t>::max()) return {};
    auto mask=std::make_shared<lib::WindowInputMask>();
    mask->pixel_width=frame.pixel_width;
    mask->pixel_height=frame.pixel_height;
    mask->opaque_pixels.resize(static_cast<std::size_t>(count),255U);
    // DはPreviewに透過指定がなければ矩形Regionを使う。AlphaData
    // にはSWTのtransparentPixelがないため、これも矩形のまま。
    if(!transparent || image.source_transparency.kind==lib::ImageTransparency::AlphaData)
        return mask;
    std::size_t bytes_per_pixel{};
    switch(frame.pixel_format) {
    case lib::PixelFormat::RGBA32:
    case lib::PixelFormat::BGRA32: bytes_per_pixel=4U; break;
    // Cの画像経路は、Previewの前にパレットと直接色の透過キーを最
    // 終RGBAのアルファへ展開する。プロバイダーがその正規化形式を渡せ
    // なければ、ここでキーを再構築せず、安全な矩形Regionを使う。
    default: return mask;
    }
    if(count>std::numeric_limits<std::size_t>::max()/bytes_per_pixel ||
       frame.pixels.size()<static_cast<std::size_t>(count)*bytes_per_pixel) return mask;
    for(std::size_t i=0;i<static_cast<std::size_t>(count);++i) {
        const auto* pixel=frame.pixels.data()+i*bytes_per_pixel;
        // パレットのインデックスと直接色のキーの別表現は、共通
        // 画像準備の経路で最終アルファチャンネルへ変換済み。
        mask->opaque_pixels[i]=std::to_integer<std::uint8_t>(pixel[3])==0U ? 0U : 255U;
    }
    return mask;
}
} // namespace

struct Preview::State {
    enum class Pending { None, Show, Hide, Dispose };
    using UploadedImage = lib::OwnedImageResource;
    Props* prop{};
    lib::Owner<lib::Widget> parent;
    lib::Services services;
    lib::Owner<lib::Window> window;
    lib::Owner<lib::ImageView> image_view;
    lib::Owner<PileImage> image;
    lib::Owner<PileImage> showing_image;
    std::shared_ptr<UploadedImage> resource;
    std::weak_ptr<lib::Window> shown_native_owner;
    bool native_owner_bound{};
    // ネイティブへの公開が失敗・再入した場合も、旧アップロードをまだ参照している可能性がある。
    std::vector<std::shared_ptr<UploadedImage>> retired_resources;
    std::uint64_t revision{}, active_revision{};
    Pending pending{Pending::None};
    Pending active_action{Pending::None};
    bool busy{}, disposed{}, cache_valid{};
    std::int32_t x{}, y{}, item_height{};
    std::int32_t shown_x{}, shown_y{}, shown_item_height{}, shown_max_width{}, shown_max_height{};
    std::uint8_t shown_alpha{};

    bool sameNativeOwner(const std::shared_ptr<lib::Window>& candidate) const noexcept {
        const std::weak_ptr<lib::Window> identity = candidate;
        return !shown_native_owner.owner_before(identity) &&
               !identity.owner_before(shown_native_owner);
    }

    void createWindow() {
        if (window || disposed) return;
        // ウィンドウ構築は共通モデルの操作であり、ネイティブUIの操作ではない。
        auto next = std::make_shared<lib::Window>();
        auto content = std::make_shared<lib::ImageView>();
        next->set_visible(false);
        next->overlay_surface = true;
        next->activate_on_show = false;
        next->always_on_top = true;
        next->resizable = next->minimizable = next->maximizable = false;
        content->scale_mode = lib::ImageScaleMode::Stretch;
        content->border = false;
        next->set_layout(std::make_shared<lib::FillLayout>());
        next->add(content);
        // 浮動する重ね表示を親のレイアウトに追加してはならない。
        image_view = std::move(content);
        window = std::move(next);
    }

    void hideDisplay(bool destroy) {
        auto old_window = window;
        auto old_view = image_view;
        auto old_resource = std::move(resource);
        auto old_retired = std::move(retired_resources);
        auto old_image = std::move(showing_image);
        cache_valid = false;
        if (destroy) {
            window.reset(); image_view.reset();
            native_owner_bound = false;
        }
        if (old_view) old_view->image = 0;
        if (old_window) old_window->set_visible(false);
        // プロバイダーを呼ぶ前に、モデルの全状態を無効にする。hide/closeが新
        // しい要求を出した場合は、これらの古いネイティブ呼び出しが戻るまで待機させる。
        std::exception_ptr failure;
        if (services.gui && old_window) {
            try { services.gui->hide(old_window); }
            catch (...) { failure = std::current_exception(); }
            if (destroy) {
                try { services.gui->close(old_window); }
                catch (...) { if (!failure) failure = std::current_exception(); }
            }
        }
        // 二つのプロバイダー呼び出しの間、旧アップロードを生存させる。
        old_resource.reset();
        if (failure) std::rethrow_exception(failure);
    }

    void display() {
        const auto ticket = revision;
        const auto source = image;
        if (disposed || !source || !prop || !prop->var().showImagePreview.value) {
            pending = disposed ? Pending::Dispose : Pending::Hide;
            return;
        }
        const auto requested_x = x, requested_y = y, requested_height = item_height;
        const auto max_width = std::max(1, prop->var().previewMaxWidth.value);
        const auto max_height = std::max(1, prop->var().previewMaxHeight.value);
        const auto alpha = static_cast<std::uint8_t>(std::clamp(prop->var().previewAlpha.value, 0, 255));
        std::shared_ptr<lib::Window> owner;
        const auto current = [&] {
            if (disposed || revision != ticket || image != source) return false;
            if ((owner && !owner->visible()) || !prop->var().showImagePreview.value) {
                pending = Pending::Hide;
                return false;
            }
            return true;
        };
        createWindow();
        auto target = window;
        auto content = image_view;
        owner = services.gui ? services.gui->window_owner(parent)
                             : std::dynamic_pointer_cast<lib::Window>(parent);
        if (!current()) return;
        // 実体化したSDL描画面は最初のネイティブ所有元を保持する。
        // 所有元が変わったら共通の描画面モデルを作り直し、全バックエ
        // ンドで新しい所有元に対応するネイティブShellを作る。
        if (native_owner_bound && !sameNativeOwner(owner)) {
            hideDisplay(true);
            if (!current()) return;
            createWindow();
            target = window;
            content = image_view;
        }
        if (cache_valid && showing_image == source && shown_x == requested_x && shown_y == requested_y &&
            shown_item_height == requested_height && shown_max_width == max_width && shown_max_height == max_height &&
            shown_alpha == alpha && target->visible() && target->owner.lock() == owner) return;

        float width = static_cast<float>(std::max(1, source->baseWidth()));
        float height = static_cast<float>(std::max(1, source->baseHeight()));
        const float factor = std::min({1.0F, static_cast<float>(max_width)/width, static_cast<float>(max_height)/height});
        width = static_cast<float>(std::max(1, clampDimension(width*factor)));
        height = static_cast<float>(std::max(1, clampDimension(height*factor)));
        float left = static_cast<float>(requested_x), top = static_cast<float>(requested_y);
        if (services.gui) {
            std::optional<lib::Rect> area;
            const auto host_options = lib::editor_window_host_options();
            if (host_options.all_mode) {
                // ALLのポインターイベントは、ブラウザで拡大・移動
                // された表示先からEditorキャンバス座標へ変換済
                // み。同じ論理空間で範囲制限する。SDLの作業領域
                // はブラウザの表示領域を表し、ここには関係しない。
                area = lib::editor_hosted_canvas_bounds(host_options);
            } else {
                const auto screens = services.gui->screens();
                if (!current()) return;
                auto screen = std::find_if(screens.begin(), screens.end(), [&](const auto& value) {
                    return contains(value.work_area, {left, top});
                });
                if (screen == screens.end()) screen = std::find_if(screens.begin(), screens.end(),
                    [](const auto& value) { return value.primary; });
                if (screen == screens.end() && !screens.empty()) screen = screens.begin();
                if (screen != screens.end()) area = screen->work_area;
            }
            if (area) {
                const auto& bounds = *area;
                if (top + height > bounds.y + bounds.height) top -= requested_height + height;
                // モニター原点が負でも、有効な画面座標。
                left = std::clamp(left, bounds.x,
                                  std::max(bounds.x, bounds.x + bounds.width - width));
                top = std::clamp(top, bounds.y,
                                 std::max(bounds.y, bounds.y + bounds.height - height));
            }
        }
        source->createImage();
        if (!current()) return;
        if (!source->isCreated()) { pending = Pending::Hide; return; }
        // 描画IDはバックエンド内だけで有効。Propsの別バックエンドでアップロード
        // したIDを代替に使わない。プロバイダーの再入をまたいで入力画素を所有する。
        const auto pixels = source->imageData().data;
        if (pixels.frames.empty() || !services.graphics) { pending = Pending::Hide; return; }
        auto prepared = std::make_shared<UploadedImage>(services.graphics);
        prepared->id = prepared->graphics->create_image(pixels);
        if (!current()) return;
        if (!prepared->id) throw std::runtime_error("Unable to prepare editor preview image");

        // GUI処理を要求する前に、完全な画像とモデルのデータを公開する
        // 。旧アップロードはネイティブ呼び出しが終わるまで生存させる。
        if (resource) retired_resources.push_back(resource);
        resource = std::move(prepared);
        cache_valid = false;
        target->owner = owner;
        target->opacity = alpha;
        target->input_mask = previewInputMask(pixels, source->transparent());
        content->image = resource->id;
        content->set_bounds({0,0,width,height});
        const lib::Rect bounds{left,top,width,height};
        target->set_bounds(bounds);
        if (services.gui) services.gui->set_window_bounds(target, bounds);
        if (!current()) return;
        target->set_visible(true);
        shown_native_owner = owner;
        native_owner_bound = true;
        if (services.gui) {
            services.gui->show(target);
            if (!current()) return;
            services.gui->request_redraw(target);
            if (!current()) return;
        }
        showing_image = source;
        shown_x = requested_x; shown_y = requested_y; shown_item_height = requested_height;
        shown_max_width = max_width; shown_max_height = max_height; shown_alpha = alpha;
        cache_valid = true;
        retired_resources.clear();
    }

    void drain() {
        if (busy) return;
        busy = true;
        std::exception_ptr failure;
        // 直列化するのはプロバイダーの副作用であり、入力の受付ではない。古いプ
        // ロバイダーがスタック上にあっても、新しい要求で保留処理を置き換える。
        while (pending != Pending::None) {
            const auto action = std::exchange(pending, Pending::None);
            active_action = action;
            active_revision = revision;
            try {
                if (action == Pending::Show && !disposed) display();
                else hideDisplay(disposed || action == Pending::Dispose);
            } catch (...) {
                cache_valid = false;
                if (!failure) failure = std::current_exception();
            }
        }
        busy = false;
        active_action = Pending::None;
        if (failure) std::rethrow_exception(failure);
    }
};

Preview::Preview(Props& prop, lib::Owner<lib::Widget> parent, lib::Services services)
    : state_(std::make_shared<State>()) {
    if (!services.gui && !services.graphics && !services.image_codecs) services = prop.images().services();
    state_->prop = &prop;
    state_->parent = std::move(parent);
    state_->services = std::move(services);
    state_->createWindow();
}

Preview::~Preview() {
    const auto state = state_;
    state->disposed = true;
    ++state->revision;
    const auto retired_image = std::move(state->image);
    state->pending = State::Pending::Dispose;
    try { state->drain(); } catch (...) {}
}

const lib::Owner<PileImage>& Preview::getImage() const noexcept { return state_->image; }
const lib::Owner<lib::Window>& Preview::window() const noexcept { return state_->window; }
bool Preview::visible() const noexcept { return state_->window && state_->window->visible(); }

void Preview::image(lib::Owner<PileImage> value, const std::int32_t x,
                    const std::int32_t y, const std::int32_t item_height) {
    const auto state = state_;
    if (state->disposed) return;
    if (!value) { close(); return; }
    if (state->image != value || state->x != x || state->y != y || state->item_height != item_height) {
        ++state->revision;
        const auto retired_image = std::move(state->image);
        state->image = std::move(value);
        state->x = x; state->y = y; state->item_height = item_height;
    }
}

void Preview::show() {
    const auto state = state_;
    if (state->disposed || (state->busy && state->active_revision == state->revision &&
                            state->pending == State::Pending::None)) return;
    state->pending = State::Pending::Show;
    state->drain();
}

void Preview::close() noexcept {
    const auto state = state_;
    if (state->disposed) return;
    if (!state->image && (state->pending == State::Pending::Hide || state->pending == State::Pending::Dispose ||
        (state->busy && (state->active_action == State::Pending::Hide || state->active_action == State::Pending::Dispose)) ||
        (!state->busy && !state->resource && (!state->window || !state->window->visible())))) return;
    ++state->revision;
    const auto retired_image = std::move(state->image);
    state->pending = State::Pending::Hide;
    // 任意のネイティブプロバイダーが失敗しても、closeや破棄でアプリケーシ
    // ョンを終了させない。drainは引き続き、新しい待機中の操作を完了する。
    try { state->drain(); } catch (...) {}
}

void Preview::dispose() noexcept {
    const auto state = state_;
    if (state->disposed) return;
    if (!state->image && (state->pending == State::Pending::Dispose ||
        (state->busy && state->active_action == State::Pending::Dispose))) return;
    ++state->revision;
    const auto retired_image = std::move(state->image);
    state->pending = State::Pending::Dispose;
    try { state->drain(); } catch (...) {}
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::areaviewutils
