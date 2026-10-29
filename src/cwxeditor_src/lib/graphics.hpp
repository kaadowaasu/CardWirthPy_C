#pragma once

#include "types.hpp"

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cwxeditor::lib {

enum class StrokeStyle { Solid, Dash, Dot, DashDot };
enum class FillRule { EvenOdd, Winding };
enum class CompositeMode { Normal, Mask, Add, Subtract, Multiply };
enum class GradientAxis { None, Horizontal, Vertical };

class DrawingContext {
public:
    virtual ~DrawingContext() = default;
    virtual void save() = 0;
    virtual void restore() = 0;
    virtual void clip(Rect rect) = 0;
    virtual void set_antialias(bool) {}
    virtual void clear(Color color) = 0;
    virtual void draw_line(Point from, Point to, Color color, float width, StrokeStyle style) = 0;
    virtual void draw_rect(Rect rect, Color color, float width, StrokeStyle style) = 0;
    // XORには出力先の画素読み出しが必要。画面表示を行わないコンテキストや記録用コン
    // テキストでは未対応としてよく、呼び出し側は共通のフォーカス表示代替処理を使う。
    virtual bool draw_xor_rect(Rect,Color,float) {return false;}
    virtual void fill_rect(Rect rect, Color color) = 0;
    virtual void draw_polygon(std::span<const Point>, Color, float, StrokeStyle) {}
    virtual void fill_polygon(std::span<const Point>, Color, FillRule = FillRule::Winding) {}
    virtual void draw_ellipse(Rect, Color, float, StrokeStyle) {}
    virtual void fill_ellipse(Rect, Color) {}
    virtual void fill_gradient(Rect rect, Color first, Color second,
                               GradientAxis axis, CompositeMode mode) {
        (void)second;
        (void)axis;
        (void)mode;
        fill_rect(rect, first);
    }
    virtual void draw_text(std::string_view text, Point origin, const FontSpec& font, Color color) = 0;
    virtual void draw_text_rotated(std::string_view text, Point origin,
                                   const FontSpec& font, Color color,
                                   float degrees) {
        (void)degrees;
        draw_text(text, origin, font, color);
    }
    virtual void draw_image(ResourceId image, Rect source, Rect destination, Interpolation interpolation) = 0;
    // 不透明度指定の描画は省略可能とし、既存の独自コンテキストとのソース互換性を保つ。
    virtual void draw_image_with_alpha(ResourceId image, Rect source, Rect destination,
                                       Interpolation interpolation, std::uint8_t alpha) {
        (void)alpha;
        draw_image(image, source, destination, interpolation);
    }
    [[nodiscard]] virtual Size measure_text(std::string_view text, const FontSpec& font, float wrap_width) = 0;
    [[nodiscard]] virtual FontMetrics font_metrics(const FontSpec& font) {
        const auto height=measure_text(" ",font,0).height;
        return {height,height,0};
    }
};

struct DecodeImageRequest {
    std::filesystem::path path;
    std::span<const std::byte> encoded_data{};
    ImageFormat format_hint{ImageFormat::Unknown};
    std::optional<Size> requested_size;
};

struct EncodeImageRequest {
    std::filesystem::path path;
    ImageFormat format{ImageFormat::Png};
    float quality{0.9F};
};

class ImageCodecBackend {
public:
    virtual ~ImageCodecBackend() = default;
    [[nodiscard]] virtual ImageData decode(const DecodeImageRequest& request, std::string& error) = 0;
    virtual bool encode(const ImageData& image, const EncodeImageRequest& request, std::string& error) = 0;
    [[nodiscard]] virtual Size probe(std::span<const std::byte> encoded_data, ImageFormat hint) = 0;
};

class GraphicsBackend {
public:
    virtual ~GraphicsBackend() = default;
    virtual ResourceId create_image(const ImageData& image) = 0;
    virtual void update_image(ResourceId id, const ImageData& image) = 0;
    virtual void destroy_image(ResourceId id) noexcept = 0;
    [[nodiscard]] virtual std::vector<std::string> font_families() const = 0;
    [[nodiscard]] virtual Size measure_text(std::string_view text, const FontSpec& font, float wrap_width) = 0;
    [[nodiscard]] virtual FontMetrics font_metrics(const FontSpec& font) {
        const auto height=measure_text(" ",font,0).height;
        return {height,height,0};
    }
    // nulloptは拡大縮小可能なグリフパスをプロバイダーが提供
    // しないことを表す。空のパスは空白などの有効な空グリフを表す。
    [[nodiscard]] virtual std::optional<GlyphPath> glyph_path(char32_t,const FontSpec&) {return std::nullopt;}
    /// 既存の画像フレームに文字をラスタライズする。プラットフォーム側
    /// でネイティブのフォントエンジンに置き換えられる。既定の共通実装
    /// はヘッドレスやデスクトップ以外の実装とのソース互換性を保つ。
    virtual bool draw_text_to_image(ImageData&, std::size_t, std::string_view,
                                    Rect, const FontSpec&, Color) {
        return false;
    }
    // 共通形式の画素だけを取り出す。この処理はワーカーで実行してよいが、create_image
    // とdestroy_imageは描画/UIの所有スレッドで実行する。
    [[nodiscard]] virtual ImageData extract_application_icon_data(
        const std::filesystem::path&, Size) { return {}; }
    [[nodiscard]] virtual ResourceId load_application_icon(const std::filesystem::path& executable,
                                                            Size requested_size) = 0;
};

/// バックエンドが所有する一つのアップロード資源の寿命を共有する。借
/// 用したキャッシュIDではない。プロバイダー呼び出し前に確保し、返
/// されたIDを一度だけ設定する。このオブジェクトの共有により描画
/// や公開中も作成元を生存させ、別のバックエンドには付け替えない。
struct OwnedImageResource final {
    explicit OwnedImageResource(std::shared_ptr<GraphicsBackend> creator)
        : graphics(std::move(creator)) {}
    OwnedImageResource(const OwnedImageResource&) = delete;
    OwnedImageResource& operator=(const OwnedImageResource&) = delete;
    ~OwnedImageResource() { if (graphics && id) graphics->destroy_image(id); }
    const std::shared_ptr<GraphicsBackend> graphics;
    ResourceId id{};
};

/// 置換可能なバックエンドのアップロード資源。代替画素の準備中も公
/// 開中の画像を有効に保ち、同じバックエンドでの更新では資源IDを
/// 維持する。リビジョン確認により同期コールバックの再入を扱い、入
/// れ子のpublish/resetを古い呼び出しより優先する。
class ImageResourceSlot final {
public:
    ImageResourceSlot() = default;
    ImageResourceSlot(const ImageResourceSlot&) = delete;
    ImageResourceSlot& operator=(const ImageResourceSlot&) = delete;
    ~ImageResourceSlot() { reset(); }

    [[nodiscard]] ResourceId id() const noexcept { return id_; }
    [[nodiscard]] const std::shared_ptr<GraphicsBackend>& graphics() const noexcept {
        return graphics_;
    }

    ResourceId publish(std::shared_ptr<GraphicsBackend> graphics,
                       const ImageData& image) {
        if (!graphics || image.frames.empty()) {
            reset();
            return 0U;
        }
        const auto request = ++revision_;
        const auto old_graphics = graphics_;
        const auto old_id = id_;
        ResourceId next_id{};
        bool created{};
        if (old_id != 0U && old_graphics == graphics) {
            graphics->update_image(old_id, image);
            next_id = old_id;
        } else {
            next_id = graphics->create_image(image);
            created = next_id != 0U;
        }
        if (request != revision_) {
            if (created) graphics->destroy_image(next_id);
            return id_;
        }
        // メモリ確保に失敗しても、最後に完成したプレビューを表示し続ける。
        if (next_id == 0U) return id_;
        const bool replaced = old_id != 0U &&
            (old_id != next_id || old_graphics != graphics);
        graphics_ = std::move(graphics);
        id_ = next_id;
        if (replaced && old_graphics)
            old_graphics->destroy_image(old_id);
        return id_;
    }

    void reset() noexcept {
        ++revision_;
        auto graphics = std::move(graphics_);
        const auto id = id_;
        id_ = 0U;
        if (graphics && id != 0U) graphics->destroy_image(id);
    }

private:
    std::shared_ptr<GraphicsBackend> graphics_;
    ResourceId id_{};
    std::uint64_t revision_{};
};

}  // namespace cwxeditor::lib
