#pragma once
#include "cellrender.hpp"

// エリア表示の画像・状態文字列・モデルを使う背景編集の、移植可能なC++20
// 実装。 ネイティブGUIのハンドルは、このAPIへ渡さない。
#include "cwxeditor_src/core/cwx/area.hpp"
#include "cwxeditor_src/core/cwx/background.hpp"
#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/graphics.hpp"
#include "cwxeditor_src/lib/image_composition.hpp"
#include "cwxeditor_src/lib/layout.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::areaview { class BgImagesView; }

namespace cwxeditor::cwx::editor::gui::dwt::areaviewutils {

namespace lib = ::cwxeditor::lib;
namespace area = ::cwxeditor::cwx::area;
namespace background = ::cwxeditor::cwx::background;
namespace card = ::cwxeditor::cwx::card;
namespace dprops = ::cwxeditor::cwx::editor::gui::dwt::dprops;
namespace skin = ::cwxeditor::cwx::skin;
namespace summary = ::cwxeditor::cwx::summary;
namespace structs = ::cwxeditor::cwx::structs;
namespace types = ::cwxeditor::cwx::types;
namespace usecounter = ::cwxeditor::cwx::usecounter;

using Props = dprops::Props;
using Skin = skin::Skin;
using Summary = summary::Summary;
using UseCounter = usecounter::UseCounter;
using UseCounterPtr = usecounter::CounterOwner;
using AbstractArea = area::AbstractArea;
using AbstractSpCard = area::AbstractSpCard;
using MenuCard = area::MenuCard;
using EnemyCard = area::EnemyCard;
using CastCard = card::CastCard;
using CardImage = card::CardImage;
using CardImageArray = area::CardImageArray;
using BgImage = background::BgImage;
using BgImageContainer = background::BgImageContainer;
using String = lib::String;
using StringView = lib::StringView;

struct ImageDataWithScale {
    lib::ImageData data;
    lib::ResourceId resource{};
    lib::Size logical_size{};
    std::uint32_t source_scale{1U};
    bool loaded{};
    bool owned_resource{};
    ImageDataWithScale() = default;
    explicit ImageDataWithScale(lib::ImageData value, std::uint32_t scale = 1U);
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] lib::Size size() const noexcept;
    void clear() noexcept;
};

using ScaleType = lib::ImageLayerScale;

class PileImage : public std::enable_shared_from_this<PileImage> {
public:
    struct Append {
        String path;
        ImageDataWithScale image;
        structs::CInsets insets{};
        structs::CPoint point{};
        ScaleType scale_type{ScaleType::Cut};
        std::uint8_t alpha{255U};
        std::uint32_t drawing_scale{1U};
        bool transparent{};
        bool positioned{};
    };
    PileImage(String path, std::uint32_t target_scale, std::int32_t x,
              std::int32_t y, std::int32_t base_width, std::int32_t base_height,
              bool scale_mode = false, bool data_resizable = false);
    PileImage(ImageDataWithScale image, std::uint32_t target_scale,
              std::int32_t x, std::int32_t y, std::int32_t base_width,
              std::int32_t base_height, bool scale_mode = false,
              bool data_resizable = false);
    virtual ~PileImage();
    PileImage(const PileImage&) = delete;
    PileImage& operator=(const PileImage&) = delete;
    PileImage(PileImage&&) noexcept;
    PileImage& operator=(PileImage&&) noexcept;
    [[nodiscard]] const String& path() const noexcept { return path_; }
    void path(String value) { path_ = std::move(value); }
    [[nodiscard]] bool updatePath(StringView old_path, StringView new_path);
    [[nodiscard]] std::uint32_t targetScale() const noexcept { return target_scale_; }
    void targetScale(std::uint32_t value) noexcept;
    [[nodiscard]] std::uint32_t scale() const noexcept { return scale_; }
    void scale(std::uint32_t value) noexcept;
    [[nodiscard]] std::int32_t x() const noexcept { return x_; }
    void x(std::int32_t value) noexcept { x_ = value; }
    [[nodiscard]] std::int32_t y() const noexcept { return y_; }
    void y(std::int32_t value) noexcept { y_ = value; }
    [[nodiscard]] std::int32_t baseWidth() const noexcept { return base_width_; }
    [[nodiscard]] std::int32_t baseHeight() const noexcept { return base_height_; }
    void baseWidth(std::int32_t value) noexcept { base_width_ = value; }
    void baseHeight(std::int32_t value) noexcept { base_height_ = value; }
    [[nodiscard]] std::int32_t width() const noexcept { return width_; }
    [[nodiscard]] std::int32_t height() const noexcept { return height_; }
    void width(std::int32_t value) noexcept { width_ = value; }
    void height(std::int32_t value) noexcept { height_ = value; }
    [[nodiscard]] std::int32_t layer() const noexcept { return layer_; }
    void layer(std::int32_t value) noexcept { layer_ = value; }
    [[nodiscard]] lib::Rect bounds() const noexcept;
    void bounds(lib::Rect value) noexcept;
    [[nodiscard]] bool scaleMode() const noexcept { return scale_mode_; }
    [[nodiscard]] bool dataResizable() const noexcept { return data_resizable_; }
    [[nodiscard]] bool transparent() const noexcept { return transparent_; }
    void transparent(bool value) noexcept;
    void sourceColorKeyPrepared(bool value) noexcept { color_key_prepared_=value; invalidateImage(); }
    [[nodiscard]] bool smoothing() const noexcept { return smoothing_; }
    void smoothing(bool value) noexcept { smoothing_ = value; }
    [[nodiscard]] std::int32_t alpha() const noexcept { return alpha_; }
    void alpha(std::int32_t value) noexcept;
    [[nodiscard]] bool separator() const noexcept { return separator_; }
    void separator(bool value) noexcept { separator_ = value; }
    [[nodiscard]] bool visible() const noexcept { return visible_; }
    void visible(bool value) noexcept { visible_ = value; }
    void append(String path, structs::CInsets insets,
                ScaleType scale_type = ScaleType::Cut, bool transparent = false,
                std::int32_t mask_x = 0, std::int32_t mask_y = 0,
                std::uint8_t alpha = 255U, std::uint32_t drawing_scale = 1U);
    // 共通Sceneで画像の復号とカラーキー処理
    // が済んでいても、元の素材パスを保持する。
    void append(String path, ImageDataWithScale image, structs::CInsets insets,
                ScaleType scale_type = ScaleType::Cut,
                std::uint8_t alpha = 255U, bool transparent = false,
                std::uint32_t drawing_scale = 1U);
    void append(ImageDataWithScale image, structs::CInsets insets,
                 ScaleType scale_type = ScaleType::Cut,
                 std::uint8_t alpha = 255U, bool transparent = false,
                 std::uint32_t drawing_scale = 1U);
    void append(ImageDataWithScale image, structs::CPoint point,
                ScaleType scale_type = ScaleType::Cut,
                std::uint8_t alpha = 255U);
    [[nodiscard]] const std::vector<Append>& appends() const noexcept { return appends_; }
    void setBaseImage(ImageDataWithScale image, std::int32_t base_width,
                      std::int32_t base_height);
    [[nodiscard]] const ImageDataWithScale& imageData() const noexcept { return image_; }
    [[nodiscard]] const ImageDataWithScale& baseSizeData() const noexcept { return base_image_; }
    [[nodiscard]] lib::ResourceId resource() const noexcept { return image_.resource; }
    // 資源IDはこのサービスに属する。 プレビューを開いたまま失
    // われ得るサービスを生ポインターで借りず、寿命を保持する。
    void setGraphics(lib::Owner<lib::GraphicsBackend> graphics) noexcept;
    [[nodiscard]] lib::GraphicsBackend* graphics() const noexcept { return graphics_.get(); }
    void sourceServices(lib::Services services) { source_services_=std::move(services); }
    [[nodiscard]] const lib::Services& sourceServices() const noexcept { return source_services_; }
    void setTitle(String title, structs::CFont font, lib::Point point,
                  std::int32_t shrink_width = 0, bool antialias = true);
    [[nodiscard]] const String& title() const noexcept { return title_; }
    [[nodiscard]] const structs::CFont& titleFont() const noexcept { return title_font_; }
    [[nodiscard]] lib::Point titlePoint() const noexcept { return title_point_; }
    [[nodiscard]] std::int32_t titleShrinkWidth() const noexcept { return title_shrink_width_; }
    [[nodiscard]] bool titleAntialias() const noexcept { return title_antialias_; }
    [[nodiscard]] lib::Color titleColor()const noexcept{return title_color_;}
    void titleColor(lib::Color color)noexcept{title_color_=color;invalidateImage();}
    [[nodiscard]] structs::CRGB titleHemmingColor()const noexcept{return title_hemming_color_;}
    void titleHemmingColor(structs::CRGB color)noexcept{title_hemming_color_=color;invalidateImage();}
    [[nodiscard]] bool isCreated() const noexcept { return created_; }
    [[nodiscard]] std::shared_ptr<cellrender::TextRasterCache> textRasterCache() const {return text_cache_;}
    virtual void createImage();
    virtual void clearImage() noexcept;
    virtual void resize();

protected:
    void invalidateImage() noexcept;
    void releaseResource() noexcept;
    String path_;
    ImageDataWithScale image_;
    ImageDataWithScale base_image_;
    std::vector<Append> appends_;
    std::uint32_t target_scale_{1U};
    std::uint32_t scale_{100U};
    std::int32_t x_{};
    std::int32_t y_{};
    std::int32_t base_width_{};
    std::int32_t base_height_{};
    std::int32_t width_{};
    std::int32_t height_{};
    std::int32_t layer_{};
    bool scale_mode_{};
    bool data_resizable_{};
    bool transparent_{};
    bool color_key_prepared_{};
    bool smoothing_{};
    std::int32_t alpha_{255};
    bool separator_{};
    bool visible_{true};
    bool created_{};
    std::shared_ptr<cellrender::TextRasterCache> text_cache_{std::make_shared<cellrender::TextRasterCache>()};
    std::shared_ptr<void> lifetime_{std::make_shared<int>(0)};
    lib::Owner<lib::GraphicsBackend> graphics_;
    lib::Services source_services_;
    std::uint64_t image_revision_{};
    bool creating_image_{};
    bool title_assigned_{}; // Dと同じく、nullと明示的に空のタイトルを区別する。
    String title_;
    structs::CFont title_font_{};
    lib::Point title_point_{};
    std::int32_t title_shrink_width_{};
    bool title_antialias_{true};
    lib::Color title_color_{0,0,0,255};
    structs::CRGB title_hemming_color_{0,0,0,0};
};

class FlexImage final : public PileImage {
public:
    using PileImage::PileImage;
    using PileImage::scale;
    std::uint32_t minimumScale{1U};
    std::uint32_t maximumScale{1000U};
    std::uint32_t newScale{100U};
    std::int32_t newWidth{};
    std::int32_t newHeight{};
    bool hasSelectionFilter{};
    // DのFlexImageは保留倍率を変え、resize()で確定す
    // る。 PileImageの設定関数で先にscale_を書いても、
    // resize()が古いnewScaleで置き換えるため使わない。
    void scale(std::uint32_t value) noexcept {
        newScale = value;
        resize();
    }
    void resize() override;
};
using FlexImagePtr = lib::Owner<FlexImage>;

struct Commons {
    Props* prop{};
    const Skin* skin{};
    const Summary* summary{};
    lib::Services services{};
    std::size_t undoMaxEvent{100U};
    std::function<std::size_t()> undoMaxProvider;
    std::function<lib::Action(lib::Action)> subscribeUndoMax;
    lib::Action refreshToolBar;
    String undoText{"元に戻す"};
    String redoText{"やり直す"};
    String upText{"上へ"};
    String downText{"下へ"};
    String refreshText{"更新"};
    // 埋め込みEditor部品が作る補助ダイアログの、
    // 任意の所有者。 共通モデルで所有関係を保持し、各
    // 表示実装で同じ親・中央配置の規則を使う。 子部品
    // の生ポインターからOSのウィンドウを探さない。
    lib::Owner<lib::Window> dialogOwner{};
    // C++ホスト向けの拡張として、キャッシュ済みの装飾画像を
    // 渡せる。 D版は常にpreviewMessageで描く。
    lib::Callback<ImageDataWithScale(const Summary*)> messageImage;
    std::function<bool(lib::StringView, bool)> open_cwx_path;
    Commons() = default;
    explicit Commons(Props& value) : prop(&value), services(value.images().services()) {}
    Commons(Props& value, const Skin& value_skin)
        : prop(&value), skin(&value_skin), services(value.images().services()) {}
};

[[nodiscard]] lib::Owner<FlexImage> createBackgroundImage(
    Props& prop, const Skin& skin, const Summary* summ, StringView path,
    std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height,
    bool transparent, std::int32_t layer, bool smoothing);
[[nodiscard]] lib::Owner<PileImage> createCastCardBackImage(
    Props& prop, Skin& skin, const Summary* summ, std::int32_t x,
    std::int32_t y, std::uint8_t alpha);
void updateCastCardBackImage(PileImage& image, Skin& skin, std::uint8_t alpha,
                             std::uint32_t target_scale);
namespace detail {
[[nodiscard]] std::uint8_t cardNameAverage(const lib::ImageData&,structs::CRect);
[[nodiscard]] lib::Point toLibPoint(structs::CPoint value) noexcept;
[[nodiscard]] ImageDataWithScale detailLoadImage(Props& prop, const Skin& skin,
                                                  const Summary* summ, StringView path,
                                                  std::uint32_t target_scale,
                                                  lib::Size fallback = {});
[[nodiscard]] ImageDataWithScale detailLoadImage(Props& prop, const Skin& skin,
                                                  const Summary* summ, StringView path,
                                                  std::uint32_t target_scale,
                                                  lib::Size fallback,
                                                  bool transparent,
                                                  bool publish_resource = false,
                                                  bool is_image_cell = false);
[[nodiscard]] ImageDataWithScale detailCastCardImage(
    Props& prop, const Skin& skin, const Summary* summ,
    const EnemyCard* enemy, const CastCard* card, bool debug_mode);
[[nodiscard]] ImageDataWithScale detailMenuCardImage(Props& prop,
                                                     const Skin& skin,
                                                     const Summary* summ);
[[nodiscard]] ImageDataWithScale detailTalkerImage(Props& prop,
                                                   const Skin& skin,
                                                   const Summary* summ,
                                                   types::Talker talker);
// 場面MenuCardのTalker.Cardも、dutils::addToPileImage
// と同じ カード寸法・内側余白0の配置・アルファの規則を使う。
void appendTalkerCardImage(PileImage& target, const CardImage& image,
                           Props& prop, const Skin& skin,
                           const Summary* summ);
[[nodiscard]] ImageDataWithScale detailPcNumberImage(Props& prop,
                                                     std::uint32_t number, bool legacy = false);
}  // namespace detail

template <typename PImg>
[[nodiscard]] lib::Owner<PImg> createCardImageCommon(
    Props& prop, const Summary*, ImageDataWithScale card,
    structs::CInsets mat_pad, int x, int y, std::uint32_t scale,
    bool smoothing, int layer) {
    const auto size = prop.looks().cardSize();
    const auto width = static_cast<std::int32_t>(size.width) + mat_pad.e + mat_pad.w;
    const auto height = static_cast<std::int32_t>(size.height) + mat_pad.n + mat_pad.s;
    auto result = std::make_shared<PImg>(std::move(card), prop.drawingScale(),
                                         x, y, width, height, true, true);
    result->setGraphics(prop.images().services().graphics);
    result->transparent(false);
    result->smoothing(smoothing);
    result->layer(layer * 10);
    if constexpr (std::is_base_of_v<FlexImage, PImg>) {
        result->maximumScale = static_cast<std::uint32_t>(std::max(1, prop.var().cardScaleMax.value));
        result->minimumScale = static_cast<std::uint32_t>(std::max(1, prop.var().cardScaleMin.value));
        result->newScale = scale;
        // D版と同じく、カード画像では拡大率だけを次回サイズとして渡す。
        // newWidth/newHeight は背景セルの自由変形用であり、ここで指定すると
        // 固定寸法が newScale より優先され、シーン上のカードだけ拡大されない。
    } else {
        result->scale(scale);
    }
    return result;
}

template <typename PImg>
[[nodiscard]] lib::Owner<PImg> createCastCardImage(
    const Commons& comm, const Skin& skin, const Summary* summ,
    const EnemyCard* enemy, const CastCard* card, int x, int y,
    std::uint32_t scale, bool smoothing, bool debug_mode, int layer) {
    if (comm.prop == nullptr) return {};
    auto result = createCardImageCommon<PImg>(*comm.prop, summ,
        detail::detailCastCardImage(*comm.prop, skin, summ, enemy, card, debug_mode),
        comm.prop->looks().castCardInsets(), x, y, scale, smoothing, layer);
    // detailCastCardImageは、Dと同じ枠・
    // 顔・体力バー・初期状態を合成済みの画像を返す。 ここで
    // 顔画像を重ねると状態表示を覆ってしまうため追加しない。
    // 共通のcastCardImageは名前と輪郭も画像に
    // 含むので、場面専用の文字を重ねず同じ結果を再利用する。
    if constexpr (std::is_base_of_v<FlexImage, PImg>) {
        result->hasSelectionFilter = comm.prop->var().showSceneViewSelectionFilter.value;
        result->resize();
    } else {
        result->createImage();
    }
    return result;
}

template <typename PImg>
[[nodiscard]] lib::Owner<PImg> createMenuCardImage(
    Props& prop, const Skin& skin, const Summary* summ, StringView title,
    const CardImageArray& paths, int x, int y, std::uint32_t scale,
    bool smoothing, int layer) {
    auto result = createCardImageCommon<PImg>(prop, summ,
        detail::detailMenuCardImage(prop, skin, summ), prop.looks().menuCardInsets(),
        x, y, scale, smoothing, layer);
    for (const auto& path : paths) {
        if (!path) continue;
        if (path->type == types::CardImageType::File && !path->path().empty()) {
            auto scale_type = ScaleType::Cut;
            if (path->positionType() == types::CardImagePosition::Center)
                scale_type = ScaleType::Center;
            bool skin_material = false;
            bool engine_material = false;
            (void)skin.findImagePathF(
                path->path(), summ ? summ->scenarioPath() : String{},
                summ ? summ->dataVersion()
                     : ::cwxeditor::cwx::versioninfo::LATEST_VERSION,
                skin_material, engine_material);
            const auto image_scale = skin_material || engine_material
                ? prop.drawingScale() : prop.drawingScaleForImage(summ);
            // DのCardImage.addToPileImageは、拡大前の
            // 素材へCardWirthの左上の色によるカラーキーを適用する。
            // 拡縮後にキー処理をせず、そこで得たアルファを重ね合わせでも保つ。
            result->append(path->path(), detail::detailLoadImage(
                               prop, skin, summ, path->path(), image_scale, {}, true),
                           prop.looks().menuCardInsets(), scale_type, 255U, false,
                           image_scale);
        } else if (path->type == types::CardImageType::PCNumber &&
                   path->pcNumber() > 0U) {
            result->append(detail::detailPcNumberImage(prop, path->pcNumber(), skin.legacy()),
                           prop.looks().menuCardInsets(), ScaleType::Center, 255U,
                           false, prop.drawingScale());
        } else if (path->type == types::CardImageType::Talker) {
            if (path->talker() == types::Talker::Card) {
                detail::appendTalkerCardImage(*result, *path, prop, skin, summ);
            } else {
                result->append(detail::detailTalkerImage(prop, skin, summ,
                                                         path->talker()),
                               prop.looks().menuCardInsets(),
                               ScaleType::Cut, 255U, true, prop.drawingScale());
            }
        }
    }
    const auto point = prop.looks().menuCardNamePoint();
    result->setTitle(String(title), prop.adjustFont(prop.looks().menuCardNameFont(skin.legacy())),
                     detail::toLibPoint(point), skin.legacy() ? 0 :
                     static_cast<std::int32_t>(result->baseSizeData().size().width) - point.x * 2,
                     !skin.legacy());
    if(!skin.legacy()) {
        // Dと同じく、装飾前の名前領域のピクセルから文字色を求める。
        const auto area=prop.looks().cardNameArea();
        const auto& source=result->baseSizeData();
        const auto scale=std::max(1U,source.source_scale);
        const structs::CRect pixels{area.x*int(scale),area.y*int(scale),area.width*int(scale),area.height*int(scale)};
        if(detail::cardNameAverage(source.data,pixels)<prop.var().negativeCardNameBorder.value) {
            result->titleColor({255,255,255,255});
            result->titleHemmingColor(prop.var().cardNameBorderingColorForWhite.value);
        } else result->titleHemmingColor(prop.var().cardNameBorderingColorForBlack.value);
    }
    if constexpr (std::is_base_of_v<FlexImage, PImg>) {
        result->hasSelectionFilter = prop.var().showSceneViewSelectionFilter.value;
        result->resize();
    } else {
        result->createImage();
    }
    return result;
}

// 公開の補助関数も製品の背景エディターを使い、別のCanvasを実装しない。
using BgImagesView = ::cwxeditor::cwx::editor::gui::dwt::areaview::BgImagesView;

[[nodiscard]] lib::Owner<BgImagesView> createBgImagesViewAndMenu(
    Commons comm, Props& prop, const Summary* summ, const Skin& skin,
    UseCounterPtr counter, BgImageContainer& container, lib::Container& parent,
    AbstractArea* ref_target, bool show_inherit_backs, bool read_only);
[[nodiscard]] lib::Owner<PileImage> createMessageImage(const Commons& comm,
                                                        Props& prop,
                                                        const Summary* summ);
[[nodiscard]] String createFlagName(const Commons& comm, const Summary* summ,
                                    UseCounterPtr counter, StringView path);
[[nodiscard]] String createAreaViewStatusLine(const Commons& comm,
                                              const Summary* summ,
                                              const Skin& skin,
                                              const MenuCard& card);
[[nodiscard]] String createAreaViewStatusLine(const Commons& comm,
                                              const Summary* summ,
                                              const Skin& skin,
                                              const EnemyCard& card);
[[nodiscard]] String statusParams(const Commons& comm, const Summary* summ,
                                  const AbstractSpCard& card);
[[nodiscard]] String createAreaViewStatusLine(const Commons& comm,
                                              const Summary* summ,
                                              const Skin& skin,
                                              const BgImage& back);
[[nodiscard]] String statusParams(const Commons& comm, const Summary* summ,
                                  const BgImage& back);
class Preview final {
public:
    Preview(Props& prop, lib::Owner<lib::Widget> parent = {},
            lib::Services services = {});
    ~Preview();
    Preview(const Preview&) = delete;
    Preview& operator=(const Preview&) = delete;
    void image(lib::Owner<PileImage> value, std::int32_t x,
               std::int32_t y, std::int32_t item_height);
    [[nodiscard]] const lib::Owner<PileImage>& getImage() const noexcept;
    [[nodiscard]] const lib::Owner<lib::Window>& window() const noexcept;
    [[nodiscard]] bool visible() const noexcept;
    void dispose() noexcept;
    void show();
    void close() noexcept;

private:
    // 提供元の呼び出し中に、所有するArea/Directoryビューが同期的に削除
    // される場合がある。 実行中は生のPreview*ではなくStateを保持する。
    struct State;
    std::shared_ptr<State> state_;
};


}  // namespace cwxeditor::cwx::editor::gui::dwt::areaviewutils
