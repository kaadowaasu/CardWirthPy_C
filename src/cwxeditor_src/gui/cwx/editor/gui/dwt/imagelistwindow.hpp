#pragma once

// cwx/editor/gui/dwt/imagelistwindow.d
// の移植可能なC++20実装。 SWTのComposite
// ・Shell・GC・ドラッグのハンドルを共
// 通部品とサービスで表す。 配置・当たり判定
// ・選択・スクロール・画像資源は通常のC++
// 状態として保持し、ヘッドレスでも使える。

#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/versioninfo.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/drag_drop.hpp"
#include "cwxeditor_src/lib/graphics.hpp"
#include "cwxeditor_src/lib/gui.hpp"
#include "cwxeditor_src/lib/ui_actions.hpp"
#include "cwxeditor_src/lib/layout.hpp"
#include "cwxeditor_src/lib/portable.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::imagelistwindow {

using CProps = ::cwxeditor::cwx::props::CProps;
using Skin = ::cwxeditor::cwx::skin::Skin;
using Summary = ::cwxeditor::cwx::summary::Summary;

/// Dのテンプレートで使う素材分類。ImageListWindow
/// は列挙・整数の非型引数を受け付け、 生成済み互換ヘッダーな
/// しにmaterialselect::MtTypeも渡せる。
enum class ImageListType { CARD, BG_IMG, BGM, SE };
using MtType = ImageListType;

/// dutils.ImageDataWithScaleに対応する共通型。
struct ImageDataWithScale {
    lib::ImageData data;
    lib::ResourceId resource{};
    lib::Size logical_size{};
    std::uint32_t source_scale{1};
    bool loaded{};
    bool owned_resource{};

    ImageDataWithScale() = default;
    explicit ImageDataWithScale(lib::ImageData value,
                                std::uint32_t scale = 1U)
        : data(std::move(value)), source_scale(scale == 0U ? 1U : scale),
          loaded(true) {}

    [[nodiscard]] bool valid() const noexcept {
        return loaded && (resource != 0U || !data.frames.empty());
    }
    [[nodiscard]] lib::Size size() const noexcept;
    void clear() noexcept {
        data = {};
        resource = 0U;
        logical_size = {};
        loaded = false;
        owned_resource = false;
    }
};

struct ImageTile {
    lib::String path;
    lib::String label;
    ImageDataWithScale image;
    lib::Rect bounds{};
    lib::Rect image_bounds{};
    bool selected{};
};

struct ScrollState {
    float offset{};
    float minimum{};
    float maximum{};
    float thumb{};
    float increment{1.0F};
    float page_increment{1.0F};
};

/// 仮想化された画像グリッド。DWTの描画経路と同じく、 paint()
/// が可視タイルを訪れた時点で画像を遅延生成する。
class ImageList : public lib::Canvas, public std::enable_shared_from_this<ImageList> {
public:
    static constexpr float SPACING = 10.0F;
    static constexpr std::size_t no_selection =
        static_cast<std::size_t>(-1);

    using ImageFactory =
        lib::Callback<ImageDataWithScale(lib::StringView, bool)>;

    explicit ImageList(lib::Size viewport = {});
    ~ImageList() override;
    ImageList(const ImageList&) = delete;
    ImageList& operator=(const ImageList&) = delete;

    [[nodiscard]] lib::Size viewport() const noexcept { return viewport_; }
    void setViewport(lib::Size value);
    [[nodiscard]] lib::Size contentSize() const noexcept { return content_size_; }

    void init(float image_width, float image_height, ImageFactory create_image,
              float image_scale = 1.0F, int drawing_scale = 1);
    void init(std::int32_t image_width, std::int32_t image_height,
              ImageFactory create_image, float image_scale = 1.0F,
              int drawing_scale = 1) {
        init(static_cast<float>(image_width), static_cast<float>(image_height),
             std::move(create_image), image_scale, drawing_scale);
    }

    [[nodiscard]] std::size_t calcCountPerLine() const noexcept;
    void calcScrollParams();
    [[nodiscard]] const ScrollState& scrollState() const noexcept {
        return scroll_;
    }
    void setScrollOffset(float value);
    [[nodiscard]] float scrollOffset() const noexcept { return scroll_.offset; }

    [[nodiscard]] lib::String path(std::size_t index) const;
    [[nodiscard]] const lib::Array<lib::String>& paths() const noexcept {
        return paths_;
    }
    [[nodiscard]] const lib::Array<ImageTile>& tiles() const noexcept {
        return tiles_;
    }
    [[nodiscard]] std::ptrdiff_t selectedIndex() const noexcept {
        return selected_;
    }
    [[nodiscard]] lib::String selectedPath() const;

    /// valueと等しい最初のパスを選ぶ。空の値なら、 DのImageList.select
    /// と同じく検索前に選択を解除する。
    bool select(lib::StringView value);
    void showSelection();
    [[nodiscard]] std::ptrdiff_t indexOf(int x, int y) const;

    [[nodiscard]] bool mask() const noexcept { return mask_; }
    void mask(bool value);
    void add(const lib::Array<lib::String>& paths);
    // 既定ではDのimages設定と同じく読み直す。 ドラッグでは
    // 全行を復号し直さず、未変更の画像の利用権と表示位置を保つ。
    void replace(lib::Array<lib::String> paths, lib::String selected_path = {},
                 bool preserve_existing = false);
    void invalidatePendingPaint() noexcept { ++image_revision_; }
    // 提供元で解決したドロップは、同じ行・配置を指し続ける必要がある。 再
    // 入した置き換えが同じパス群を持っていても、別の世代として区別する。
    [[nodiscard]] std::uint64_t presentationRevision() const noexcept { return image_revision_; }
    void removeAll();
    bool insert(std::size_t index, lib::String path);
    bool removeAt(std::size_t index);
    bool move(std::size_t from, std::size_t to);

    void setFont(lib::FontSpec value) { font_ = std::move(value); ++image_revision_; }
    [[nodiscard]] float fontHeight() const noexcept { return font_height_; }
    void setFontHeight(float value);
    void setCharacterWidth(float value);

    void setGraphics(lib::Owner<lib::GraphicsBackend> value);
    [[nodiscard]] lib::GraphicsBackend* graphics() const noexcept {
        return graphics_.get();
    }

    /// タイルの範囲を更新し、可視の画像資源とラベルを描く。
    void layoutTiles();
    void paint(lib::DrawingContext& context,
               lib::Rect client_area = {}) const;

    /// DWT相当の手形カーソル・ツールチップ・主ボタンクリックを処理する。 ウィ
    /// ンドウのラッパーはon_item_activatedでクリック後に閉じる。
    bool handlePointer(lib::PointerEvent& event);
    bool handleScrollbarPointer(lib::PointerEvent& event);
    void cancelScrollbarInteraction();
    void setGui(std::shared_ptr<lib::GuiBackend> value) { gui_ = std::move(value); }

    lib::Callback<void(std::size_t)> on_item_activated;
    lib::Action on_items_changed;
    lib::Action on_scroll_changed;

private:
    using UploadedImage = lib::OwnedImageResource;
    struct CachedImage : ImageDataWithScale {
        std::shared_ptr<UploadedImage> upload;
    };
    [[nodiscard]] static lib::String baseName(lib::StringView value);
    [[nodiscard]] lib::Size sourceSize(const ImageDataWithScale& image) const;
    [[nodiscard]] lib::Size fitSize(lib::Size source) const;
    void ensureImage(std::size_t index) const;
    void releaseImages() noexcept;
    void notifyItemsChanged();
    [[nodiscard]] lib::ItemScrollGeometry scrollbarGeometry() const;
    [[nodiscard]] float contentWidth() const noexcept;
    void paintScrollbar(lib::DrawingContext& context) const;

    lib::Array<lib::String> paths_;
    mutable lib::Array<CachedImage> images_;
    mutable lib::Array<ImageTile> tiles_;
    lib::Size viewport_{};
    lib::Size content_size_{};
    float image_width_{};
    float image_height_{};
    bool mask_{};
    std::ptrdiff_t selected_{-1};
    ImageFactory create_image_;
    float image_scale_{1.0F};
    int drawing_scale_{1};
    bool show_selection_{};
    float font_height_{14.0F};
    float character_width_{7.0F};
    lib::FontSpec font_{};
    ScrollState scroll_{};
    std::shared_ptr<lib::GuiBackend> gui_;
    std::shared_ptr<lib::ItemScrollBarStateMachine> scroll_gesture_;
    lib::Owner<lib::GraphicsBackend> graphics_;
    std::uint64_t image_revision_{};
    mutable bool painting_{};
};

using SelectionCallback = lib::Callback<void(lib::StringView)>;
using SkinResolver =
    lib::Callback<lib::Owner<Skin>(const CProps&, const Summary*)>;
using ImageResourceProvider =
    lib::Callback<lib::ResourceId(lib::StringView, bool)>;
using WindowSizeCallback = lib::Callback<void(lib::Size)>;
using DroppedPathProvider =
    lib::Callback<lib::Array<lib::String>(const lib::DragEvent&)>;

/// DのProps.var.etcとCommonsデリゲートに代わる設定。
struct ImageListWindowOptions {
    lib::Size window_size{};
    lib::Size card_image_size{74.0F, 94.0F};
    lib::Size background_image_size{128.0F, 128.0F};
    float image_scale{1.0F};
    int drawing_scale{1};
    float font_height{14.0F};
    float character_width{7.0F};
    lib::FontSpec font{};
    lib::Owner<Skin> skin;
    SkinResolver skin_resolver;
    ImageList::ImageFactory image_factory;
    ImageResourceProvider image_resource;
    lib::Owner<lib::GraphicsBackend> graphics;
    std::shared_ptr<lib::GuiBackend> gui;
    std::shared_ptr<lib::DragDropBackend> drag_drop;
    lib::DropOperation drag_operations{lib::DropOperation::Copy |
                                       lib::DropOperation::Move};
    std::vector<std::string> accepted_formats{"text/uri-list", "text/plain"};
    bool enable_drag_drop{true};
    DroppedPathProvider dropped_paths;
    WindowSizeCallback save_window_size;
};

/// すべての素材分類ラッパーで共用する、テンプレートでない実装。
class ImageListWindowBase {
public:
    ImageListWindowBase(const CProps& prop, lib::Owner<Summary> summary,
                        lib::Owner<Skin> force_skin,
                        lib::Owner<lib::Window> parent,
                        SelectionCallback selection,
                        lib::WidgetPtr base_control,
                        ImageListWindowOptions options, bool card_type);
    ImageListWindowBase(const CProps& prop, Summary& summary,
                        lib::Owner<Skin> force_skin,
                        lib::Owner<lib::Window> parent,
                        SelectionCallback selection,
                        lib::WidgetPtr base_control,
                        ImageListWindowOptions options, bool card_type);
    ImageListWindowBase(const ImageListWindowBase&) = delete;
    ImageListWindowBase& operator=(const ImageListWindowBase&) = delete;
    ImageListWindowBase(ImageListWindowBase&&) = delete;
    ImageListWindowBase& operator=(ImageListWindowBase&&) = delete;
    virtual ~ImageListWindowBase();

    [[nodiscard]] const lib::Owner<lib::Window>& shell() const noexcept {
        return shell_;
    }
    [[nodiscard]] const lib::Owner<ImageList>& widget() const noexcept {
        return list_;
    }
    [[nodiscard]] const lib::Owner<Summary>& summaryOwner() const noexcept {
        return summary_owner_;
    }
    [[nodiscard]] const Summary* summary() const noexcept { return summary_; }
    [[nodiscard]] const CProps& props() const noexcept { return *prop_; }
    [[nodiscard]] lib::Owner<Skin> summSkin() const {
        const auto life = callback_lifetime_;
        const auto revision = life->revision;
        if (life->owner != this) return {};
        const auto forced = force_skin_;
        const auto resolver = options_.skin_resolver;
        const auto* prop = prop_;
        const auto* summary = summary_;
        const auto fallback = summ_skin_;
        if (forced) return forced;
        if (!resolver || !prop) return fallback;
        auto current = resolver(*prop, summary);
        // 解決関数は呼び出し元の処理であり、同期的にラッパーを閉じたり
        // 破棄したりできる。 戻った後は事前に取得した状態だけを使う。
        if (!life->owner || life->revision != revision) return {};
        // 有効な解決関数がある場合は、スキンを解決できなかった場合もその結果を優先する。
        return current;
    }
    [[nodiscard]] const lib::Owner<Skin>& forceSkin() const noexcept {
        return force_skin_;
    }
    [[nodiscard]] bool cardType() const noexcept { return card_type_; }
    [[nodiscard]] bool closed() const noexcept { return closed_; }

    void open();
    void close();
    void images(lib::String dir, lib::Array<lib::String> paths);
    void select(lib::StringView path);
    void mask(bool value);
    [[nodiscard]] bool mask() const noexcept;
    void refImageScale();
    // 所有するImageSelectのCommons通知から得た、現在のPropsの値。
    void refImageScale(lib::Size image_size, float image_scale, int drawing_scale);
    [[nodiscard]] ImageDataWithScale createImage(lib::StringView path,
                                                  bool mask) const;
    [[nodiscard]] lib::Array<lib::String> defExts() const;
    [[nodiscard]] lib::Array<lib::String> defDirs() const;

    [[nodiscard]] lib::Size imageSize() const noexcept;
    void setWindowBounds(lib::Rect value);
    [[nodiscard]] lib::Rect windowBounds() const noexcept;
    void layout();

    [[nodiscard]] bool addPath(lib::String path);
    bool removePath(std::size_t index);
    bool movePath(std::size_t from, std::size_t to);
    [[nodiscard]] const lib::String& title() const noexcept;

    /// 共通のドラッグイベントを、バックエンドへ接続したのと同じ処理へ送る。
    bool handleDrop(lib::DragEvent& event);
    [[nodiscard]] lib::ClipboardData dragData() const;

    lib::Action on_closed;
    lib::Callback<void(lib::StringView)> on_selected;
    lib::Action on_images_changed;
    lib::Callback<void(const lib::DragEvent&)> on_drag_drop;

protected:
    void initialize();
    // UI通知で単独所有のラッパーが同期的に破棄される場合がある。 トー
    // クンは実行中の呼び出しより長く残るが、ラッパーや親は保持しない。
    struct CallbackLifetime {
        ImageListWindowBase* owner{};
        std::uint64_t revision{};
        bool binding{};
    };
    std::shared_ptr<CallbackLifetime> callback_lifetime_ = std::make_shared<CallbackLifetime>();
    [[nodiscard]] ImageList::ImageFactory imageFactory() const;
    [[nodiscard]] bool hostEffectivelyVisible() const;
    void watchHostVisibility();
    void bindDragDrop();
    void unbindDragDrop() noexcept;
    void handleActivation(std::size_t index);
    [[nodiscard]] lib::Array<lib::String> extractDroppedPaths(
        const lib::DragEvent& event) const;
    bool reorderDropped(lib::Array<lib::String> paths, std::size_t target,
                        lib::DropOperation operation);
    [[nodiscard]] bool hasOperation(lib::DropOperation value,
                                    lib::DropOperation bit) const noexcept;

    const CProps* prop_{};
    lib::Owner<Summary> summary_owner_;
    const Summary* summary_{};
    lib::Owner<Skin> summ_skin_;
    lib::Owner<Skin> force_skin_;
    lib::Owner<lib::Window> parent_;
    lib::WidgetPtr base_control_;
    SelectionCallback selection_;
    ImageListWindowOptions options_;
    lib::Owner<lib::Window> shell_;
    lib::Owner<ImageList> list_;
    bool card_type_{};
    bool closed_{true};
    bool drag_bound_{};
    bool standalone_visibility_{};
    std::vector<std::weak_ptr<lib::Widget>> visibility_observers_;
};

/// DのTypeに対応する引数はautoの非型値とし、 このモジュールのImageListType
/// とmaterialselect::MtTypeの両方を受け付ける。
template <auto Type = ImageListType::CARD>
class ImageListWindow final : public ImageListWindowBase {
public:
    static constexpr bool card_type = static_cast<int>(Type) ==
                                      static_cast<int>(ImageListType::CARD);

    ImageListWindow(const CProps& prop, lib::Owner<Summary> summary = {},
                    lib::Owner<Skin> force_skin = {},
                    lib::Owner<lib::Window> parent = {},
                    SelectionCallback selection = {},
                    lib::WidgetPtr base_control = {},
                    ImageListWindowOptions options = {})
        : ImageListWindowBase(prop, std::move(summary), std::move(force_skin),
                              std::move(parent), std::move(selection),
                              std::move(base_control), std::move(options),
                              card_type) {}

    ImageListWindow(const CProps& prop, Summary& summary,
                    lib::Owner<Skin> force_skin = {},
                    lib::Owner<lib::Window> parent = {},
                    SelectionCallback selection = {},
                    lib::WidgetPtr base_control = {},
                    ImageListWindowOptions options = {})
        : ImageListWindowBase(prop, summary, std::move(force_skin),
                              std::move(parent), std::move(selection),
                              std::move(base_control), std::move(options),
                              card_type) {}
};

using CardImageListWindow = ImageListWindow<ImageListType::CARD>;
using BackgroundImageListWindow = ImageListWindow<ImageListType::BG_IMG>;

}  // namespace cwxeditor::cwx::editor::gui::dwt::imagelistwindow
