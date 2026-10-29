#pragma once
#include "cellrender.hpp"

// cwx/editor/gui/dwt/images.dの移植
// 可能なC++20実装。 公開APIはEditorの値型と共
// 通libの部品・描画を使い、OSの部品ハンドルは含めない。

#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dskin.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/graphics.hpp"
#include "cwxeditor_src/lib/image_composition.hpp"
#include "cwxeditor_src/lib/services.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::images {

namespace lib = ::cwxeditor::lib;
namespace structs = ::cwxeditor::cwx::structs;
namespace types = ::cwxeditor::cwx::types;
namespace dskin = ::cwxeditor::cwx::editor::gui::dwt::dskin;

using String = lib::String;
using StringView = lib::StringView;
using Point = structs::CPoint;
using Rectangle = structs::CRect;
using Insets = structs::CInsets;
using RGB = lib::Color;
using ImageDataWithScale = dskin::ImageDataWithScale;

inline constexpr std::uint32_t NORMAL_SCALE = 1U;

enum class Toggle {
    LEFT_TOP, LEFT_MIDDLE, LEFT_BOTTOM, MIDDLE_TOP, MIDDLE_BOTTOM,
    RIGHT_TOP, RIGHT_MIDDLE, RIGHT_BOTTOM, MOVE, NONE,
};
using ScaleType = lib::ImageLayerScale;
enum class ImageType { Image, Text, ColorFilter, Drawer };

struct FontData {
    String name;
    std::int32_t height{};
    std::int32_t style{};
};

class PileImage {
public:
    enum class TPos { LEFT, RIGHT };
    using Drawer = std::function<void(const PileImage&, lib::DrawingContext&)>;
    using PreviewText = std::function<String(StringView)>;

    struct Append {
        Insets insets{};
        String path;
        String text;
        ImageDataWithScale data;
        bool transparent{};
        std::int32_t mask_x{};
        std::int32_t mask_y{};
        structs::CFont font{};
        structs::CRGB font_color{};
        TPos text_pos{TPos::LEFT};
        bool antialias{};
        std::uint8_t alpha{255U};
        FontData font_data{};
        bool has_font_data{};
        ScaleType scale_type{ScaleType::Scale};
        std::uint32_t drawing_scale{};
        std::function<std::uint32_t()> drawing_scale_override;
    };

    PileImage(String path, std::uint32_t target_scale, std::int32_t x,
              std::int32_t y, std::int32_t base_width, std::int32_t base_height,
              bool scale_mode = false);
    PileImage(String path, std::uint32_t target_scale, std::int32_t base_width,
              std::int32_t base_height, bool scale_mode = false);
    PileImage(ImageDataWithScale data, std::uint32_t target_scale,
              std::int32_t x, std::int32_t y, std::int32_t base_width,
              std::int32_t base_height, bool scale_mode = false,
              bool data_resizable = true);
    PileImage(ImageDataWithScale data, std::uint32_t target_scale,
              std::int32_t base_width, std::int32_t base_height,
              bool scale_mode = false);
    PileImage(ImageType type, std::uint32_t target_scale, std::int32_t x,
              std::int32_t y, std::int32_t base_width, std::int32_t base_height,
              bool scale_mode = false);
    PileImage(String text, std::uint32_t target_scale, String font_name,
              std::int32_t size, structs::CRGB color, bool bold, bool italic,
              bool underline, bool strike, bool vertical, bool antialias,
              types::BorderingType bordering_type, structs::CRGB bordering_color,
              std::uint32_t bordering_width, std::int32_t x, std::int32_t y,
              std::int32_t base_width, std::int32_t base_height);
    PileImage(Drawer drawer, std::uint32_t target_scale, std::int32_t x,
              std::int32_t y, std::int32_t base_width, std::int32_t base_height,
              bool scale_mode = false);
    virtual ~PileImage();

    PileImage(const PileImage&) = delete;
    PileImage& operator=(const PileImage&) = delete;
    PileImage(PileImage&&) noexcept;
    PileImage& operator=(PileImage&&) noexcept;

    [[nodiscard]] ImageType type() const noexcept { return type_; }
    [[nodiscard]] bool updatePath(StringView old_path, StringView new_path);
    [[nodiscard]] std::uint32_t targetScale() const noexcept { return target_scale_; }
    void targetScale(std::uint32_t value) noexcept;
    void setBaseImage(ImageDataWithScale data, std::int32_t base_width,
                      std::int32_t base_height);
    [[nodiscard]] std::int32_t baseWidth() const noexcept { return init_width_; }
    [[nodiscard]] std::int32_t baseHeight() const noexcept { return init_height_; }
    void baseWidth(std::int32_t value) noexcept {
        if (init_width_ == value) return;
        init_width_ = value;
        invalidate();
    }
    void baseHeight(std::int32_t value) noexcept {
        if (init_height_ == value) return;
        init_height_ = value;
        invalidate();
    }
    [[nodiscard]] bool scaleMode() const noexcept { return scale_mode_; }

    void append(String path, Insets insets, ScaleType scale_type,
                bool transparent, std::int32_t mask_x = 0,
                std::int32_t mask_y = 0, std::uint8_t alpha = 255U,
                std::uint32_t drawing_scale = 0U);
    void append(String path, Insets insets, ScaleType scale_type,
                bool transparent, std::int32_t mask_x, std::int32_t mask_y,
                std::uint8_t alpha, std::function<std::uint32_t()> drawing_scale);
    void append(ImageDataWithScale data, Insets insets,
                ScaleType scale_type, std::uint8_t alpha = 255U);
    void append(ImageDataWithScale data, Point point, ScaleType scale_type,
                std::uint8_t alpha = 255U);
    void append(FontData font_data, String text, Insets insets);
    void append(String text, Insets insets, structs::CFont font,
                structs::CRGB font_color, bool antialias, TPos pos);
    void append(String text, Point point, structs::CFont font,
                structs::CRGB font_color, bool antialias);

    void setPath(String path);
    void setPath(std::size_t index, String path);
    void setTransparent(std::size_t index, bool value);
    void setImageData(ImageDataWithScale data);
    void setImageData(std::size_t index, ImageDataWithScale data);

    void setTitle(String title, structs::CFont font, Point point,
                  std::int32_t shrink_width = 0, bool antialias = false);
    void setTitle(String title, String font_name, std::int32_t size,
                  bool bold, bool italic, bool vertical);
    [[nodiscard]] const String& title() const noexcept { return title_; }
    void title(String value) {
        if (title_ == value) return;
        title_ = std::move(value);
        invalidate();
    }
    [[nodiscard]] const std::optional<RGB>& titleColor() const noexcept { return title_color_; }
    void titleColor(std::optional<RGB> value) {
        const bool same = title_color_.has_value() == value.has_value() &&
            (!value || (title_color_->red == value->red &&
                        title_color_->green == value->green &&
                        title_color_->blue == value->blue &&
                        title_color_->alpha == value->alpha));
        if (same) return;
        title_color_ = value;
        invalidate();
    }
    [[nodiscard]] structs::CRGB titleHemmingColor() const noexcept { return title_hemming_color_; }
    void titleHemmingColor(structs::CRGB value) noexcept {
        if (title_hemming_color_ == value) return;
        title_hemming_color_ = value;
        invalidate();
    }
    [[nodiscard]] const structs::CFont& font() const noexcept { return title_font_; }
    [[nodiscard]] std::int32_t fontPixelSize() const noexcept { return font_pixel_size_; }

    void colorMask(std::int32_t r, std::int32_t g, std::int32_t b,
                   std::int32_t a) noexcept;
    [[nodiscard]] structs::CRGB textColor() const noexcept { return text_color_; }
    void textColor(structs::CRGB value) noexcept {
        if (text_color_ == value) return;
        text_color_ = value;
        invalidate();
    }
    [[nodiscard]] bool underline() const noexcept { return underline_; }
    void underline(bool value) noexcept {
        if (underline_ == value) return;
        underline_ = value;
        invalidate();
    }
    [[nodiscard]] bool strike() const noexcept { return strike_; }
    void strike(bool value) noexcept {
        if (strike_ == value) return;
        strike_ = value;
        invalidate();
    }
    [[nodiscard]] bool vertical() const noexcept { return vertical_; }
    void vertical(bool value) noexcept {
        if (vertical_ == value) return;
        vertical_ = value;
        invalidate();
    }
    [[nodiscard]] bool antialias() const noexcept { return antialias_; }
    void antialias(bool value) noexcept {
        if (antialias_ == value) return;
        antialias_ = value;
        invalidate();
    }
    [[nodiscard]] types::BorderingType borderingType() const noexcept { return bordering_type_; }
    void borderingType(types::BorderingType value) noexcept {
        if (bordering_type_ == value) return;
        bordering_type_ = value;
        invalidate();
    }
    [[nodiscard]] structs::CRGB borderingColor() const noexcept { return bordering_color_; }
    void borderingColor(structs::CRGB value) noexcept {
        if (bordering_color_ == value) return;
        bordering_color_ = value;
        invalidate();
    }
    [[nodiscard]] std::uint32_t borderingWidth() const noexcept { return bordering_width_; }
    void borderingWidth(std::uint32_t value) noexcept {
        if (bordering_width_ == value) return;
        bordering_width_ = value;
        invalidate();
    }
    [[nodiscard]] const PreviewText& previewText() const noexcept { return preview_text_; }
    void previewText(PreviewText value) { preview_text_ = std::move(value); invalidate(); }

    [[nodiscard]] types::BlendMode blendMode() const noexcept { return blend_mode_; }
    void blendMode(types::BlendMode value) noexcept {
        if (blend_mode_ == value) return;
        blend_mode_ = value;
        invalidate();
    }
    [[nodiscard]] types::GradientDir gradientDir() const noexcept { return gradient_dir_; }
    void gradientDir(types::GradientDir value) noexcept {
        if (gradient_dir_ == value) return;
        gradient_dir_ = value;
        invalidate();
    }
    [[nodiscard]] structs::CRGB color1() const noexcept { return color1_; }
    void color1(structs::CRGB value) noexcept {
        if (color1_ == value) return;
        color1_ = value;
        invalidate();
    }
    [[nodiscard]] structs::CRGB color2() const noexcept { return color2_; }
    void color2(structs::CRGB value) noexcept {
        if (color2_ == value) return;
        color2_ = value;
        invalidate();
    }

    [[nodiscard]] const ImageDataWithScale& imageData() const;
    [[nodiscard]] ImageDataWithScale& imageData();
    [[nodiscard]] bool isCreated() const noexcept { return created_; }
    void createImage();
    void clearImage() noexcept;
    [[nodiscard]] ImageDataWithScale createImageData() const;
    void draw(lib::DrawingContext& context, Rectangle range = {}) const;

    [[nodiscard]] bool isOpaque() const;
    [[nodiscard]] bool isOpaqueWithoutAlpha() const;
    [[nodiscard]] lib::ResourceId image() const noexcept { return image_data_.resource; }
    [[nodiscard]] const ImageDataWithScale& baseSizeData() const noexcept { return base_size_data_; }

    [[nodiscard]] std::int32_t layer() const noexcept { return layer_; }
    void layer(std::int32_t value) noexcept { layer_ = value; }
    [[nodiscard]] bool visible() const noexcept { return visible_; }
    void visible(bool value) noexcept { visible_ = value; }
    [[nodiscard]] bool smoothing() const noexcept { return smoothing_; }
    void smoothing(bool value) noexcept {
        if (smoothing_ == value) return;
        smoothing_ = value;
        invalidate();
    }
    [[nodiscard]] bool transparent() const noexcept { return transparent_; }
    void transparent(bool value) noexcept {
        if (transparent_ == value) return;
        transparent_ = value;
        invalidate();
    }
    [[nodiscard]] std::int32_t alpha() const noexcept { return alpha_; }
    void alpha(std::int32_t value) noexcept { const auto next=std::clamp(value,0,255);if(alpha_!=next){alpha_=next;invalidate();} }
    [[nodiscard]] bool separator() const noexcept { return separator_; }
    void separator(bool value) noexcept { separator_ = value; }
    [[nodiscard]] std::int32_t x() const noexcept { return rect_.x; }
    void x(std::int32_t value) noexcept { rect_.x = value; }
    [[nodiscard]] std::int32_t y() const noexcept { return rect_.y; }
    void y(std::int32_t value) noexcept { rect_.y = value; }
    [[nodiscard]] std::int32_t width() const noexcept;
    void width(std::int32_t value) noexcept;
    [[nodiscard]] std::int32_t height() const noexcept;
    void height(std::int32_t value) noexcept;
    [[nodiscard]] Rectangle bounds() const noexcept;
    void bounds(Rectangle value) noexcept;
    void boundsWithScale(std::int32_t x, std::int32_t y, std::uint32_t scale) noexcept;
    [[nodiscard]] std::uint32_t scale() const noexcept { return scale_; }
    void scale(std::uint32_t value) noexcept {
        value = std::max(1U, value);
        if (scale_ == value) return;
        scale_ = value;
        invalidate();
    }
    [[nodiscard]] const String& path() const noexcept { return path_; }
    void setGraphics(lib::GraphicsBackend* graphics) noexcept;
    [[nodiscard]] lib::GraphicsBackend* graphics() const noexcept { return graphics_; }
    void setServices(lib::Services services) noexcept;
    [[nodiscard]] const std::vector<Append>& appends() const noexcept { return appends_; }
    void dispose() noexcept { clearImage(); }

protected:
    // 描画コールバック後に、画像ペインがこの画像の寿命を確認する。
    friend class ImagePane;
    [[nodiscard]] ImageDataWithScale createImageDataImpl()const;
    void invalidate() const noexcept {
        ++image_revision_;
        image_dirty_ = true;
    }
    [[nodiscard]] ImageDataWithScale loadPath(StringView path, std::uint32_t scale,
                                               bool transparent, std::int32_t mask_x,
                                               std::int32_t mask_y) const;
    /// 指定された倍率を返す。D形式の呼び出しでtargetScale
    /// == 0なら、現在のホストのDPI倍率を返す。
    [[nodiscard]] std::uint32_t positiveScale() const noexcept;
    void releaseResource() const noexcept;

    ImageType type_{ImageType::Image};
    String path_;
    ImageDataWithScale data_;
    mutable ImageDataWithScale image_data_;
    mutable ImageDataWithScale base_size_data_;
    std::vector<Append> appends_;
    std::uint32_t target_scale_{NORMAL_SCALE};
    std::uint32_t scale_{100U};
    Rectangle rect_{};
    std::int32_t layer_{};
    std::int32_t init_width_{};
    std::int32_t init_height_{};
    bool scale_mode_{};
    bool data_resizable_{true};
    bool transparent_{};
    bool smoothing_{};
    std::int32_t alpha_{255};
    bool separator_{};
    bool visible_{true};
    mutable bool image_dirty_{true};
    mutable bool created_{};
    mutable std::vector<std::shared_ptr<cellrender::TextRasterCache>> text_caches_;
    std::shared_ptr<void> lifetime_{std::make_shared<int>(0)};
    mutable std::uint64_t image_revision_{};
    bool creating_image_{};
    lib::GraphicsBackend* graphics_{};
    mutable lib::Services services_;
    String title_;
    structs::CFont title_font_{};
    Point title_point_{};
    std::int32_t title_shrink_width_{};
    bool title_antialias_{};
    std::optional<RGB> title_color_;
    structs::CRGB title_hemming_color_{0, 0, 0, 0};
    std::int32_t mask_r_{}, mask_g_{}, mask_b_{}, mask_a_{};
    std::int32_t font_pixel_size_{};
    structs::CRGB text_color_{0, 0, 0, 255};
    bool underline_{};
    bool strike_{};
    bool vertical_{};
    bool antialias_{};
    types::BorderingType bordering_type_{types::BorderingType::None};
    structs::CRGB bordering_color_{255, 255, 255, 255};
    std::uint32_t bordering_width_{1U};
    PreviewText preview_text_;
    mutable String created_preview_text_;
    types::BlendMode blend_mode_{types::BlendMode::Normal};
    types::GradientDir gradient_dir_{types::GradientDir::None};
    structs::CRGB color1_{255, 255, 255, 255};
    structs::CRGB color2_{0, 0, 0, 255};
    Drawer drawer_;
};

class FlexImage final : public PileImage {
public:
    using PileImage::PileImage;
    using ResizeListener = std::function<void(FlexImage&, std::int32_t, std::int32_t,
                                               std::int32_t, std::int32_t)>;
    using ScaleResizeListener = std::function<void(FlexImage&, std::int32_t,
                                                    std::int32_t, std::uint32_t)>;
    using SelectionListener = std::function<void(FlexImage&)>;

    std::int32_t minimum_width{};
    std::int32_t minimum_height{};
    std::int32_t maximum_width{65536};
    std::int32_t maximum_height{65536};
    std::uint32_t maximum_scale{300U};
    std::uint32_t minimum_scale{50U};
    bool selected{};
    bool has_selection_filter{};
    bool fixed{};
    bool keep_ratio{};
    std::uint32_t new_scale{100U};
    std::uint32_t toggle_size{5U};

    [[nodiscard]] std::int32_t minimumWidth() const noexcept { return minimum_width; }
    void minimumWidth(std::int32_t value) noexcept { minimum_width = std::max(0, value); }
    [[nodiscard]] std::int32_t minimumHeight() const noexcept { return minimum_height; }
    void minimumHeight(std::int32_t value) noexcept { minimum_height = std::max(0, value); }
    [[nodiscard]] std::int32_t maximumWidth() const noexcept { return maximum_width; }
    void maximumWidth(std::int32_t value) noexcept { maximum_width = std::max(0, value); }
    [[nodiscard]] std::int32_t maximumHeight() const noexcept { return maximum_height; }
    void maximumHeight(std::int32_t value) noexcept { maximum_height = std::max(0, value); }
    [[nodiscard]] std::uint32_t minimumScale() const noexcept { return minimum_scale; }
    void minimumScale(std::uint32_t value) noexcept { minimum_scale = std::max(1U, value); }
    [[nodiscard]] std::uint32_t maximumScale() const noexcept { return maximum_scale; }
    void maximumScale(std::uint32_t value) noexcept { maximum_scale = std::max(minimum_scale, value); }
    [[nodiscard]] bool isSelected() const noexcept { return selected; }
    void setSelected(bool value) noexcept { selected = value; }
    void resize(bool call_listeners = true);
    void reset() noexcept;
    [[nodiscard]] std::int32_t newX() const noexcept;
    void newX(std::int32_t value) noexcept;
    [[nodiscard]] std::int32_t newY() const noexcept;
    void newY(std::int32_t value) noexcept;
    [[nodiscard]] std::int32_t newWidth() const noexcept;
    void newWidth(std::int32_t value) noexcept;
    [[nodiscard]] std::int32_t newHeight() const noexcept;
    void newHeight(std::int32_t value) noexcept;
    [[nodiscard]] Rectangle newBounds() const noexcept;
    void newBounds(Rectangle value) noexcept;
    void newBoundsWithScale(std::int32_t x, std::int32_t y,
                            std::uint32_t scale) noexcept;
    [[nodiscard]] std::uint32_t newScale() const noexcept { return new_scale; }
    void newScale(std::uint32_t value) noexcept;
    [[nodiscard]] std::uint32_t roundScale(std::uint32_t value) const noexcept;
    [[nodiscard]] std::int32_t roundMWidth(std::int32_t value) const noexcept;
    [[nodiscard]] std::int32_t roundMHeight(std::int32_t value) const noexcept;
    [[nodiscard]] Toggle inToggle(std::int32_t x, std::int32_t y, bool move) const noexcept;
    [[nodiscard]] Rectangle drawNewArea() const noexcept;
    [[nodiscard]] Rectangle drawArea() const noexcept;
    void drawToggle(lib::DrawingContext& context, std::int32_t image_scale,
                    bool draw_xor_selection_line) const;
    void addResizeListener(ResizeListener listener) { resize_listeners_.push_back(std::move(listener)); }
    void addResizeListener(ScaleResizeListener listener) { scale_resize_listeners_.push_back(std::move(listener)); }
    void addSelectionListener(SelectionListener listener) { selection_listeners_.push_back(std::move(listener)); }

private:
    void retoggle() noexcept;
    [[nodiscard]] Rectangle toggleRect(Toggle value) const noexcept;
    mutable bool pending_initialized_{};
    Rectangle pending_{};
    std::vector<ResizeListener> resize_listeners_;
    std::vector<ScaleResizeListener> scale_resize_listeners_;
    std::vector<SelectionListener> selection_listeners_;
    friend class ImagePane;
};

class ImagePane final : public lib::Canvas {
public:
    using ImagePtr = lib::Owner<PileImage>;
    using RangeSelectable = std::function<bool(const FlexImage&)>;

    explicit ImagePane(std::int32_t width = 0, std::int32_t height = 0,
                       std::int32_t image_scale = 1,
                       std::int32_t drawing_scale = 1);
    ImagePane(lib::Container* parent, std::int32_t style, std::int32_t width,
              std::int32_t height, std::int32_t image_scale,
              std::int32_t drawing_scale);
    ~ImagePane() override = default;

    void paint(lib::DrawingContext& context, Rectangle client_area = {}) const;
    bool handlePointer(lib::PointerEvent& event);
    bool handleKey(lib::KeyEvent& event);
    void redrawAll();
    void addRedraw(std::int32_t x, std::int32_t y, std::int32_t width,
                   std::int32_t height);
    void cancelFullRedraw() noexcept { cancel_full_redraw_ = true; }

    [[nodiscard]] const std::vector<PileImage*>& images() const noexcept;
    [[nodiscard]] std::vector<PileImage*>& images() noexcept;
    void swap(std::size_t first, std::size_t second);
    void select(const std::vector<FlexImage*>& values);
    void select(FlexImage* value);
    void select(const std::vector<std::int32_t>& indices);
    void deselect(FlexImage* value);
    void deselectAll();
    void deselectRange(std::size_t from, std::size_t to);
    [[nodiscard]] std::vector<std::int32_t> selectedIndices() const;
    [[nodiscard]] std::int32_t selectedIndex() const;
    [[nodiscard]] std::int32_t findIndex(std::int32_t x, std::int32_t y) const;
    [[nodiscard]] std::vector<std::int32_t> findIndices(std::int32_t x, std::int32_t y) const;
    [[nodiscard]] std::int32_t findSelectedIndex(std::int32_t x, std::int32_t y) const;
    [[nodiscard]] std::vector<std::int32_t> findSelectedIndices(std::int32_t x, std::int32_t y) const;
    void fixedRange(bool fixed, std::size_t from, std::size_t to);
    void remove(std::size_t index);
    void removeRange(std::size_t from, std::size_t to);
    void insert(std::size_t index, PileImage& image);
    void insert(std::size_t index, ImagePtr image);
    void insert(std::size_t index, const std::vector<PileImage*>& values);
    void append(PileImage& image);
    void append(ImagePtr image);
    void append(const std::vector<PileImage*>& values);
    void set(std::size_t index, PileImage& image);
    void set(std::size_t index, ImagePtr image);
    void changingImages(lib::Action action) { changing_images_.push_back(std::move(action)); }
    void rangeSelectable(RangeSelectable value) { range_selectable_ = std::move(value); }
    [[nodiscard]] structs::WallpaperStyle wallpaperStyle() const noexcept { return wallpaper_style_; }
    void wallpaperStyle(structs::WallpaperStyle value) noexcept { wallpaper_style_ = value; redrawAll(); }
    [[nodiscard]] const std::vector<ImageDataWithScale>& appends() const noexcept { return appends_; }
    void appends(std::vector<ImageDataWithScale> value) { appends_ = std::move(value); redrawAll(); }
    [[nodiscard]] std::int32_t gridX() const noexcept { return grid_x_; }
    void gridX(std::int32_t value) noexcept { grid_x_ = std::max(0, value); resetGrid(); redrawAll(); }
    [[nodiscard]] std::int32_t gridY() const noexcept { return grid_y_; }
    void gridY(std::int32_t value) noexcept { grid_y_ = std::max(0, value); resetGrid(); redrawAll(); }
    [[nodiscard]] std::int32_t gridRange() const noexcept { return grid_range_; }
    void gridRange(std::int32_t value) noexcept { grid_range_ = std::max(0, value); }
    [[nodiscard]] bool isMoving() const noexcept;
    void redrawImage(PileImage& image);
    void highPoint(std::int32_t value) noexcept { high_point_ = value; }
    [[nodiscard]] std::int32_t highPoint() const noexcept { return high_point_; }
    void imageScale(std::int32_t value) noexcept { image_scale_ = std::max(1, value); redrawAll(); }
    [[nodiscard]] std::int32_t imageScale() const noexcept { return image_scale_; }
    void drawingScale(std::int32_t value) noexcept { drawing_scale_ = std::max(1, value); redrawAll(); }
    [[nodiscard]] std::int32_t drawingScale() const noexcept { return drawing_scale_; }
    void drawXORSelectionLine(bool value) noexcept { draw_xor_selection_line_ = value; redrawAll(); }
    [[nodiscard]] bool drawXORSelectionLine() const noexcept { return draw_xor_selection_line_; }
    void resizingImageWithRatio(bool value) noexcept { resizing_image_with_ratio_ = value; }
    [[nodiscard]] bool resizingImageWithRatio() const noexcept { return resizing_image_with_ratio_; }
    void setBackgroundColor2(std::optional<lib::Color> value) { background_color_ = value; redrawAll(); }
    [[nodiscard]] const std::optional<lib::Color>& getBackgroundColor2() const noexcept { return background_color_; }
    void gridColor(std::optional<lib::Color> value) { grid_color_ = value; redrawAll(); }
    [[nodiscard]] const std::optional<lib::Color>& gridColor() const noexcept { return grid_color_; }
    void gridHighlightColor(std::optional<lib::Color> value) { grid_highlight_color_ = value; redrawAll(); }
    [[nodiscard]] const std::optional<lib::Color>& gridHighlightColor() const noexcept { return grid_highlight_color_; }

private:
    struct Slot { PileImage* image{}; ImagePtr owner; };
    void rebuildImageView() const;
    void resetGrid() const;
    void markImageChanged();
    void doSelect(FlexImage& image);
    void doDeselect(FlexImage& image);
    void updateDrag(FlexImage& image, Toggle toggle, std::int32_t dx,
                    std::int32_t dy, bool ratio_fix);
    [[nodiscard]] std::vector<PileImage*> orderedImages() const;

    std::vector<Slot> slots_;
    mutable std::vector<PileImage*> image_view_;
    std::vector<ImageDataWithScale> appends_;
    std::int32_t width_{};
    std::int32_t height_{};
    std::int32_t image_scale_{1};
    std::int32_t drawing_scale_{1};
    bool draw_xor_selection_line_{true};
    bool resizing_image_with_ratio_{true};
    std::int32_t grid_x_{}, grid_y_{}, grid_range_{5};
    std::int32_t high_point_{10};
    mutable std::vector<std::int32_t> grid_x_highlight_;
    mutable std::vector<std::int32_t> grid_y_highlight_;
    structs::WallpaperStyle wallpaper_style_{structs::WallpaperStyle::Tile};
    std::optional<lib::Color> background_color_;
    std::optional<lib::Color> grid_color_;
    std::optional<lib::Color> grid_highlight_color_;
    std::vector<lib::Action> changing_images_;
    RangeSelectable range_selectable_;
    mutable bool cancel_full_redraw_{};
    mutable std::vector<Rectangle> dirty_;
    FlexImage* mouse_image_{};
    Toggle drag_toggle_{Toggle::NONE};
    Point drag_start_{};
    Point range_start_{};
    Point range_end_{};
    bool range_selecting_{};
    bool moved_{};
    std::unordered_map<FlexImage*, Rectangle> drag_bounds_;
    std::uint64_t image_revision_{}, drag_generation_{};
    std::shared_ptr<int> drag_lifetime_{std::make_shared<int>(0)};
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::images
