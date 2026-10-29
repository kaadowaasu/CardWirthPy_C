#include "cwxeditor_src/gui/cwx/editor/gui/dwt/imagelistwindow.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/lib/ui_drag_drop.hpp"
#include "cwxeditor_src/lib/ui_window_lifecycle.hpp"
#include "cwxeditor_src/lib/ui_metrics.hpp"
#include "cwxeditor_src/lib/ui_text_layout.hpp"
#include "cwxeditor_src/lib/window_host.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace cwxeditor::cwx::editor::gui::dwt::imagelistwindow {

namespace {

// D版の Control.isVisible と同様、祖先とすべてのタブの選択ページを考慮する。
// Container の逆向きの接続だけでは TabView::pages を越えられないた
// め、 共有や循環を考慮して所有者のツリーを走査し、コールバック内にはそのツリーを保持しない。
struct HostVisibility { bool found{}; bool visible{}; };
HostVisibility inspectHostVisibility(const lib::WidgetPtr& root,
                                     const lib::Widget* target, bool visible,
                                     std::vector<const lib::Widget*>& path,
                                     std::vector<lib::WidgetPtr>* ancestors) {
    if (!root || std::find(path.begin(), path.end(), root.get()) != path.end()) return {};
    visible = visible && root->visible();
    if (root.get() == target) {
        if (ancestors) ancestors->push_back(root);
        return {true, visible};
    }
    path.push_back(root.get());
    HostVisibility result;
    const auto merge = [&](HostVisibility child) {
        result.found |= child.found;
        result.visible |= child.visible;
    };
    if (const auto container = std::dynamic_pointer_cast<lib::Container>(root))
        for (const auto& child : container->children())
            merge(inspectHostVisibility(child, target, visible, path, ancestors));
    if (const auto tabs = std::dynamic_pointer_cast<lib::TabView>(root))
        for (std::size_t i = 0; i < tabs->pages.size(); ++i)
            merge(inspectHostVisibility(tabs->pages[i].content, target,
                visible && tabs->selected_index == i, path, ancestors));
    path.pop_back();
    if (result.found && ancestors) ancestors->push_back(root);
    return result;
}

[[nodiscard]] float nonNegative(const float value) noexcept {
    return value < 0.0F ? 0.0F : value;
}

[[nodiscard]] bool hasBit(const lib::DropOperation value,
                           const lib::DropOperation bit) noexcept {
    return (static_cast<std::uint32_t>(value) &
            static_cast<std::uint32_t>(bit)) != 0U;
}

[[nodiscard]] lib::String fromPath(const std::filesystem::path& value) {
    return ::cwxeditor::cwx::structs::pathToUtf8(value);
}

[[nodiscard]] lib::String trimLine(lib::String value) {
    while (!value.empty() &&
           (value.back() == '\r' || value.back() == '\n' ||
            value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    std::size_t first = 0U;
    while (first < value.size() &&
           (value[first] == ' ' || value[first] == '\t')) {
        ++first;
    }
    if (first != 0U) value.erase(0U, first);
    return value;
}

[[nodiscard]] lib::String fileUriToPath(lib::String value) {
    value = trimLine(std::move(value));
    if (value.size() >= 7U && value.compare(0U, 7U, "file://") == 0) {
        value.erase(0U, 7U);
        // file:///C:/... はWindowsの通常のURI形式。UNC
        // のホスト名は残し、 ドライブ文字の前にあるURI専用のスラッシュを除く。
        if (value.size() >= 3U && value[0] == '/' &&
            ((value[1] >= 'A' && value[1] <= 'Z') ||
             (value[1] >= 'a' && value[1] <= 'z')) && value[2] == ':')
            value.erase(value.begin());
    }
    const auto hex = [](const char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    };
    lib::String decoded;
    decoded.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2U < value.size()) {
            const int high = hex(value[i + 1U]);
            const int low = hex(value[i + 2U]);
            if (high >= 0 && low >= 0) {
                decoded.push_back(static_cast<char>((high << 4) | low));
                i += 2U;
                continue;
            }
        }
        decoded.push_back(value[i]);
    }
    return decoded;
}

}  // namespace

lib::Size ImageDataWithScale::size() const noexcept {
    if (logical_size.width > 0.0F && logical_size.height > 0.0F)
        return logical_size;
    if (data.frames.empty()) return {};
    const auto& frame = data.frames.front();
    if (frame.logical_size.width > 0.0F && frame.logical_size.height > 0.0F)
        return frame.logical_size;
    const auto scale = source_scale == 0U ? 1.0F
                                         : static_cast<float>(source_scale);
    return {static_cast<float>(frame.pixel_width) / scale,
            static_cast<float>(frame.pixel_height) / scale};
}

ImageList::ImageList(const lib::Size viewport) : viewport_(viewport) {
    font_.points = 10.0F;
    set_background(lib::Color{255U, 255U, 255U, 255U});
    set_foreground(lib::Color{0U, 0U, 0U, 255U});
    set_bounds({0.0F, 0.0F, viewport_.width, viewport_.height});
    on_paint_context = [this](lib::DrawingContext& context,
                              const lib::Rect dirty) {
        const auto keep_alive = weak_from_this().lock();
        context.fill_rect(dirty, {255U, 255U, 255U, 255U});
        context.save();
        context.clip({0.0F, 0.0F, contentWidth(), viewport_.height});
        try { paint(context, dirty); }
        catch (...) { context.restore(); throw; }
        context.restore();
        paintScrollbar(context);
    };
    on_wheel = [this](lib::WheelEvent& event) {
        const auto delta = event.delta_y == 0.0F ? event.delta_x : event.delta_y;
        if (delta == 0.0F) return;
        setScrollOffset(scroll_.offset - delta * scroll_.increment);
        event.handled = true;
        if (invalidate) invalidate();
    };
    on_scroll_changed = [this] {
        if (invalidate) invalidate();
    };
    calcScrollParams();
}

ImageList::~ImageList() {
    releaseImages();
}

void ImageList::setViewport(lib::Size value) {
    value.width = nonNegative(value.width);
    value.height = nonNegative(value.height);
    viewport_ = value;
    set_bounds({0.0F, 0.0F, value.width, value.height});
    calcScrollParams();
    if (show_selection_ && visible()) showSelection();
}

void ImageList::init(const float image_width, const float image_height,
                     ImageFactory create_image, const float image_scale,
                     const int drawing_scale) {
    const auto keep_alive = weak_from_this().lock();
    lib::Array<CachedImage> retired(paths_.size());
    auto previous_factory = std::move(create_image_);
    images_.swap(retired);
    ++image_revision_;
    image_width_ = nonNegative(image_width);
    image_height_ = nonNegative(image_height);
    create_image_ = std::move(create_image);
    image_scale_ = std::max(1.0F, image_scale);
    drawing_scale_ = std::max(1, drawing_scale);
    selected_ = selected_ >= 0 &&
                        static_cast<std::size_t>(selected_) < paths_.size()
                    ? selected_
                    : -1;
    scroll_.increment = std::max(1.0F, image_height_ / 4.0F);
    calcScrollParams();
}

std::size_t ImageList::calcCountPerLine() const noexcept {
    const auto cell_width = SPACING + image_width_;
    if (cell_width <= 0.0F) return 1U;
    const auto width = contentWidth();
    auto count = static_cast<std::size_t>(std::floor(width / cell_width));
    const auto remainder = std::fmod(width, cell_width);
    if (remainder < SPACING && count > 0U) --count;
    return std::max<std::size_t>(1U, count);
}

void ImageList::calcScrollParams() {
    const auto count = calcCountPerLine();
    const auto rows = paths_.empty()
                          ? 0U
                          : (paths_.size() + count - 1U) / count;
    const auto row_height = SPACING + image_height_ + font_height_;
    content_size_ = {viewport_.width,
                     static_cast<float>(rows) * row_height + SPACING};
    scroll_.minimum = 0.0F;
    scroll_.maximum = content_size_.height;
    scroll_.thumb = viewport_.height;
    scroll_.increment = std::max(1.0F, image_height_ / 4.0F);
    scroll_.page_increment =
        std::max(scroll_.increment, viewport_.height / 2.0F);
    const auto max_offset = std::max(0.0F, scroll_.maximum - scroll_.thumb);
    scroll_.offset = std::clamp(scroll_.offset, scroll_.minimum, max_offset);
    layoutTiles();
}

void ImageList::setScrollOffset(float value) {
    const auto keep_alive = weak_from_this().lock();
    const auto max_offset = std::max(0.0F, scroll_.maximum - scroll_.thumb);
    value = std::clamp(value, scroll_.minimum, max_offset);
    if (value == scroll_.offset) return;
    scroll_.offset = value;
    layoutTiles();
    const auto callback = on_scroll_changed;
    if (callback) callback();
}

lib::String ImageList::path(const std::size_t index) const {
    if (index >= paths_.size()) throw std::out_of_range("ImageList::path");
    return paths_[index];
}

lib::String ImageList::selectedPath() const {
    if (selected_ < 0 || static_cast<std::size_t>(selected_) >= paths_.size())
        return {};
    return paths_[static_cast<std::size_t>(selected_)];
}

bool ImageList::select(const lib::StringView value) {
    const auto old = selected_;
    if (value.empty()) selected_ = -1;
    const auto it = std::find(paths_.begin(), paths_.end(), value);
    if (it != paths_.end())
        selected_ = static_cast<std::ptrdiff_t>(it - paths_.begin());
    if (old != selected_) {
        layoutTiles();
        return true;
    }
    return false;
}

void ImageList::showSelection() {
    const auto keep_alive = weak_from_this().lock();
    if (selected_ < 0 || static_cast<std::size_t>(selected_) >= paths_.size())
        return;
    if (!visible()) {
        show_selection_ = true;
        return;
    }
    const auto count = calcCountPerLine();
    const auto row_height = SPACING + image_height_ + font_height_;
    const auto row = static_cast<float>(selected_) /
                     static_cast<float>(count);
    const auto y = std::floor(row) * row_height;
    // スクロール通知前に古い要求を解除する。通知中に行の置換、 ラ
    // ッパーの破棄、別の表示位置への移動要求が発生する場合がある。
    show_selection_ = false;
    if (y < scroll_.offset) {
        setScrollOffset(y);
    } else if (scroll_.offset + scroll_.thumb <
               y + row_height + SPACING) {
        setScrollOffset(y + row_height + SPACING - scroll_.thumb);
    } else {
        layoutTiles();
    }
}

std::ptrdiff_t ImageList::indexOf(const int x, const int y) const {
    if (x < 0 || y < 0 || x >= contentWidth() || y >= viewport_.height) return -1;
    const auto count = calcCountPerLine();
    const auto cell_width = SPACING + image_width_;
    const auto row_height = SPACING + image_height_ + font_height_;
    if (cell_width <= 0.0F || row_height <= 0.0F) return -1;
    const auto adjusted_y = static_cast<float>(y) + scroll_.offset;
    const auto col = static_cast<std::size_t>(
        std::floor(static_cast<float>(x) / cell_width));
    const auto row = static_cast<std::size_t>(
        std::floor(adjusted_y / row_height));
    const auto col_position = std::fmod(static_cast<float>(x), cell_width);
    const auto row_position = std::fmod(adjusted_y, row_height);
    if (col_position <= SPACING || row_position <= SPACING || col >= count)
        return -1;
    const auto index = row * count + col;
    return index < paths_.size() ? static_cast<std::ptrdiff_t>(index) : -1;
}

void ImageList::mask(const bool value) {
    if (mask_ == value) return;
    const auto keep_alive = weak_from_this().lock();
    lib::Array<CachedImage> retired(paths_.size());
    images_.swap(retired);
    mask_ = value;
    layoutTiles();
    const auto redraw = invalidate;
    if (redraw) redraw();
}

void ImageList::add(const lib::Array<lib::String>& paths) {
    const auto keep_alive = weak_from_this().lock();
    auto next_paths = paths_;
    next_paths.insert(next_paths.end(), paths.begin(), paths.end());
    auto next_images = images_;
    next_images.resize(next_paths.size());
    auto next_tiles = tiles_;
    next_tiles.resize(next_paths.size());
    paths_.swap(next_paths);
    images_.swap(next_images);
    tiles_.swap(next_tiles);
    notifyItemsChanged();
}

void ImageList::replace(lib::Array<lib::String> paths, lib::String selected_path,
                        const bool preserve_existing) {
    const auto keep_alive = weak_from_this().lock();
    lib::Array<CachedImage> next_images(paths.size());
    lib::Array<ImageTile> next_tiles(paths.size());
    if (preserve_existing) {
        std::unordered_map<lib::StringView,std::size_t> previous;
        previous.reserve(paths_.size());
        for (std::size_t i=0;i<paths_.size();++i) previous.try_emplace(paths_[i],i);
        for (std::size_t i=0;i<paths.size();++i) {
            const auto found=previous.find(paths[i]);
            if(found!=previous.end()) next_images[i]=images_[found->second];
        }
    }
    const auto selected = selected_path.empty() ? paths.end() : std::find(paths.begin(),paths.end(),selected_path);
    const auto index = selected == paths.end() ? -1 : static_cast<std::ptrdiff_t>(selected-paths.begin());
    paths_.swap(paths);
    images_.swap(next_images);
    tiles_.swap(next_tiles);
    // D版の ImageList::removeAll は数値の
    // 選択インデックスを残す。 選択パスが存在しない場合は解除
    // し、同じ位置に入った別の行が選択表示にならないようにする
    // 。 有効化時は、現在の行への当たり判定で得たパスを使う。
    selected_ = index;
    if (!preserve_existing) scroll_.offset = 0.0F;
    notifyItemsChanged();
}

void ImageList::removeAll() {
    const auto keep_alive = weak_from_this().lock();
    auto retired = std::move(images_);
    paths_.clear();
    images_.clear();
    tiles_.clear();
    // D版は removeAll 後も数値の選択位置を保持するが、ここでは解除し、
    // 後から追加した行が古い位置を再利用しただけで選択表示にならないようにする。
    selected_ = -1;
    scroll_.offset = 0.0F;
    notifyItemsChanged();
}

bool ImageList::insert(const std::size_t index, lib::String path_value) {
    if (index > paths_.size()) return false;
    const auto keep_alive = weak_from_this().lock();
    auto next_paths = paths_;
    auto next_images = images_;
    auto next_tiles = tiles_;
    next_paths.insert(next_paths.begin() + static_cast<std::ptrdiff_t>(index), std::move(path_value));
    next_images.insert(next_images.begin() + static_cast<std::ptrdiff_t>(index), CachedImage{});
    next_tiles.insert(next_tiles.begin() + static_cast<std::ptrdiff_t>(index), ImageTile{});
    paths_.swap(next_paths);
    images_.swap(next_images);
    tiles_.swap(next_tiles);
    if (selected_ >= static_cast<std::ptrdiff_t>(index)) ++selected_;
    notifyItemsChanged();
    return true;
}

bool ImageList::removeAt(const std::size_t index) {
    if (index >= paths_.size()) return false;
    const auto keep_alive = weak_from_this().lock();
    auto retired = std::move(images_[index]);
    paths_.erase(paths_.begin() + static_cast<std::ptrdiff_t>(index));
    images_.erase(images_.begin() + static_cast<std::ptrdiff_t>(index));
    tiles_.erase(tiles_.begin() + static_cast<std::ptrdiff_t>(index));
    if (selected_ == static_cast<std::ptrdiff_t>(index)) {
        selected_ = -1;
    } else if (selected_ > static_cast<std::ptrdiff_t>(index)) {
        --selected_;
    }
    notifyItemsChanged();
    return true;
}

bool ImageList::move(const std::size_t from, std::size_t to) {
    if (from >= paths_.size() || to >= paths_.size()) return false;
    if (from == to) return true;
    const auto keep_alive = weak_from_this().lock();
    auto move_value = [from, to](auto& values) {
        auto value = std::move(values[from]);
        values.erase(values.begin() + static_cast<std::ptrdiff_t>(from));
        values.insert(values.begin() + static_cast<std::ptrdiff_t>(to),
                      std::move(value));
    };
    move_value(paths_);
    move_value(images_);
    move_value(tiles_);
    if (selected_ == static_cast<std::ptrdiff_t>(from)) {
        selected_ = static_cast<std::ptrdiff_t>(to);
    } else if (from < to && selected_ > static_cast<std::ptrdiff_t>(from) &&
               selected_ <= static_cast<std::ptrdiff_t>(to)) {
        --selected_;
    } else if (to < from && selected_ >= static_cast<std::ptrdiff_t>(to) &&
               selected_ < static_cast<std::ptrdiff_t>(from)) {
        ++selected_;
    }
    notifyItemsChanged();
    return true;
}

void ImageList::setFontHeight(const float value) {
    font_height_ = std::max(1.0F, value);
    calcScrollParams();
}

void ImageList::setCharacterWidth(const float value) {
    character_width_ = std::max(1.0F, value);
}

void ImageList::setGraphics(lib::Owner<lib::GraphicsBackend> value) {
    if (graphics_ == value) return;
    const auto keep_alive = weak_from_this().lock();
    lib::Array<CachedImage> retired(paths_.size());
    auto previous = std::move(graphics_);
    graphics_ = std::move(value);
    images_.swap(retired);
    layoutTiles();
    const auto redraw = invalidate;
    if (redraw) redraw();
    // 古いアップロードは、実行中の描画スナップショットがすべて終わるまで、生成元
    // を保持する。 古いバックエンドのIDを、新しいバックエンドで破棄しない。
}

lib::String ImageList::baseName(const lib::StringView value) {
    if (value.empty()) return {};
    const auto slash = value.find_last_of("/\\");
    return lib::String(slash == lib::StringView::npos ? value
                                                       : value.substr(slash + 1U));
}

lib::Size ImageList::sourceSize(const ImageDataWithScale& image) const {
    auto value = image.size();
    const auto target_scale = std::max(1.0F, image_scale_);
    // image.size() はすでに論理サイズ（画素数 / source_scale）を返す
    // 。 再び source_scale で割ると、@2x/@3x の素材が二重に縮小される。
    value.width *= target_scale;
    value.height *= target_scale;
    return value;
}

lib::Size ImageList::fitSize(const lib::Size source) const {
    if (source.width <= 0.0F || source.height <= 0.0F || image_width_ <= 0.0F ||
        image_height_ <= 0.0F)
        return {};
    const auto ratio = std::min(image_width_ / source.width,
                                image_height_ / source.height);
    return {source.width * std::min(1.0F, ratio),
            source.height * std::min(1.0F, ratio)};
}

void ImageList::ensureImage(const std::size_t index) const {
    if (index >= images_.size() || images_[index].loaded) return;
    const auto revision = image_revision_;
    const auto graphics = graphics_;
    const auto factory = create_image_;
    const auto source_path = paths_[index];
    const auto source_mask = mask_;
    CachedImage prepared;
    // 所有権を返す可能性のある提供処理を呼ぶ前に、所有者を確保する。
    prepared.upload = std::make_shared<UploadedImage>(graphics);
    if (factory)
        static_cast<ImageDataWithScale&>(prepared) = factory(source_path, source_mask);
    if (prepared.owned_resource) prepared.upload->id = prepared.resource;
    const auto current = [&] {
        return revision == image_revision_ && graphics == graphics_ && index < images_.size();
    };
    if (!current()) return;
    if (!prepared.owned_resource && graphics && !prepared.data.frames.empty()) {
        // 復号した画素には、この一覧の表示中に独立して
        // 無効化され得るキャッシュIDを借用しない。
        prepared.resource = graphics->create_image(prepared.data);
        prepared.upload->id = prepared.resource;
        prepared.owned_resource = prepared.resource != 0U;
        if (!current()) return;
        if (!prepared.resource) throw std::runtime_error("ImageList image upload failed");
    }
    prepared.loaded = true;
    if (index < tiles_.size()) {
        auto tile = tiles_[index];
        tile.image = prepared;
        const auto fitted = fitSize(sourceSize(prepared));
        tile.image_bounds = {
            tiles_[index].bounds.x + (image_width_ - fitted.width) / 2.0F,
            tiles_[index].bounds.y +
                (image_height_ - fitted.height) / 2.0F,
            fitted.width,
            fitted.height,
        };
        images_[index] = std::move(prepared);
        tiles_[index] = std::move(tile);
    }
}

void ImageList::releaseImages() noexcept {
    ++image_revision_;
    auto retired = std::move(images_);
    for (auto& tile : tiles_) tile.image.clear();
}

void ImageList::notifyItemsChanged() {
    calcScrollParams();
    const auto callback = on_items_changed;
    if (callback) callback();
}

void ImageList::layoutTiles() {
    ++image_revision_;
    tiles_.resize(paths_.size());
    const auto count = calcCountPerLine();
    const auto row_height = SPACING + image_height_ + font_height_;
    for (std::size_t index = 0; index < paths_.size(); ++index) {
        const auto row = index / count;
        const auto column = index % count;
        const auto x = SPACING + static_cast<float>(column) *
                                     (image_width_ + SPACING);
        const auto y = SPACING + static_cast<float>(row) * row_height -
                       scroll_.offset;
        auto& tile = tiles_[index];
        tile.path = paths_[index];
        tile.label = baseName(paths_[index]);
        tile.selected = selected_ == static_cast<std::ptrdiff_t>(index);
        tile.image = images_[index];
        tile.bounds = {x, y, image_width_, image_height_ + font_height_};
        const auto fitted = fitSize(sourceSize(tile.image));
        tile.image_bounds = {
            x + (image_width_ - fitted.width) / 2.0F,
            y + (image_height_ - fitted.height) / 2.0F,
            fitted.width,
            fitted.height,
        };
    }
}

void ImageList::paint(lib::DrawingContext& context,
                      const lib::Rect client_area) const {
    if (painting_) return;
    const auto keep_alive = weak_from_this().lock();
    painting_ = true;
    struct PaintGuard { bool& value; ~PaintGuard() { value = false; } } guard{painting_};
    const auto revision = image_revision_;
    const auto client = client_area.width > 0.0F || client_area.height > 0.0F
                            ? client_area
                            : lib::Rect{0.0F, 0.0F, viewport_.width,
                                        viewport_.height};
    for (std::size_t index = 0; index < tiles_.size(); ++index) {
        if (revision != image_revision_) return;
        const auto& tile = tiles_[index];
        if (tile.bounds.x + tile.bounds.width < client.x ||
            tile.bounds.x > client.x + client.width ||
            tile.bounds.y + tile.bounds.height < client.y ||
            tile.bounds.y > client.y + client.height)
            continue;
        ensureImage(index);
        if (revision != image_revision_) return;
        const auto rendered = tiles_[index];
        const auto upload = images_[index].upload;
        const auto font = font_;
        const auto image_width = image_width_;
        const auto image_height = image_height_;
        if (rendered.image.resource != 0U && rendered.image_bounds.width > 0.0F &&
            rendered.image_bounds.height > 0.0F) {
            auto source = rendered.image.size();
            if (!rendered.image.data.frames.empty()) {
                const auto& frame = rendered.image.data.frames.front();
                if (frame.pixel_width > 0U && frame.pixel_height > 0U)
                    source = {static_cast<float>(frame.pixel_width),
                              static_cast<float>(frame.pixel_height)};
            }
            if (source.width <= 0.0F || source.height <= 0.0F)
                source = {rendered.image_bounds.width,
                          rendered.image_bounds.height};
            context.draw_image(rendered.image.resource,
                               {0.0F, 0.0F, source.width, source.height},
                               rendered.image_bounds, lib::Interpolation::Best);
            if (revision != image_revision_) return;
        }
        if (!font.family.empty() || font.points > 0.0F) {
            const auto label = lib::ellipsize_gc_text(context, rendered.label, font, image_width);
            if (revision != image_revision_) return;
            const auto label_layout = lib::layout_gc_text(context, label, font);
            const auto measured = label_layout.extent;
            if (revision != image_revision_) return;
            const auto text_x = rendered.bounds.x +
                std::max(0.0F, (image_width - measured.width) / 2.0F);
            lib::draw_gc_text(context, label_layout,
                              {text_x, rendered.bounds.y + image_height},
                              font, {0U, 0U, 0U, 255U});
            if (revision != image_revision_) return;
        }
        if (rendered.selected) {
            context.draw_rect({rendered.bounds.x - 2.0F,
                               rendered.bounds.y - 2.0F,
                               rendered.bounds.width + 3.0F,
                               rendered.bounds.height + 3.0F},
                              {0U, 0U, 0U, 255U}, 1.0F,
                              lib::StrokeStyle::Solid);
            if (revision != image_revision_) return;
        }
    }
}

float ImageList::contentWidth() const noexcept {
    // SWT.V_SCROLL と同様、バーが無効なときもスクロールバー用の領域を確保する。
    return std::max(0.0F, viewport_.width - lib::editor_scrollbar_extent());
}

lib::ItemScrollGeometry ImageList::scrollbarGeometry() const {
    return lib::pixel_scroll_geometry({0.0F, 0.0F, viewport_.width, viewport_.height},
        {contentWidth(), content_size_.height}, {0.0F, scroll_.offset}, false, true, true);
}

void ImageList::paintScrollbar(lib::DrawingContext& context) const {
    const auto geometry = scrollbarGeometry();
    const lib::Rect track{contentWidth(), 0.0F,
        std::max(0.0F, viewport_.width - contentWidth()), viewport_.height};
    if (track.width <= 0.0F || track.height <= 0.0F) return;
    context.fill_rect(track, {238U, 240U, 244U, 255U});
    if (geometry.vertical.maximum > 0.0F)
        context.fill_rect(geometry.vertical.thumb,
            lib::scroll_thumb_pressed(*this, true)
                ? lib::Color{64U, 72U, 86U, 255U}
                : lib::Color{112U, 124U, 143U, 255U});
}

void ImageList::cancelScrollbarInteraction() {
    const auto gesture = std::move(scroll_gesture_);
    if (!gesture) return;
    // 実行中の begin/move コールバックは初期化し直さない。所有
    // 者の参照を外して無効化し、 実行中の通知がコールバックから戻るまで
    // 状態管理を保持する。 その後の継続は、識別情報のガードで拒否する。
    const auto gui = gui_;
    const auto self = weak_from_this().lock();
    if (gui && self) gui->capture_pointer(self, false);
}

bool ImageList::handleScrollbarPointer(lib::PointerEvent& event) {
    const auto self = weak_from_this().lock();
    if (!self) return false;
    if (const auto gesture = scroll_gesture_) {
        event.handled = true;
        if (event.action == lib::PointerAction::Up || event.action == lib::PointerAction::Cancel) {
            cancelScrollbarInteraction();
        } else if (event.action == lib::PointerAction::Move) {
            (void)gesture->move(event.position);
            if (!gesture->owner() && scroll_gesture_ == gesture) cancelScrollbarInteraction();
        }
        const auto redraw = invalidate;
        if (redraw) redraw();
        return true;
    }
    if (event.position.x < contentWidth() || event.position.x >= viewport_.width ||
        event.position.y < 0.0F || event.position.y >= viewport_.height) return false;
    // 無効なスクロールバー領域は、画像でも空白一覧の終了対象でもない。
    event.handled = true;
    if (event.action != lib::PointerAction::Down || event.button != lib::PointerButton::Primary)
        return true;
    auto gesture = std::make_shared<lib::ItemScrollBarStateMachine>();
    auto expected = std::make_shared<std::uint64_t>(image_revision_);
    const auto weak = weak_from_this();
    lib::ScrollBarAccess access;
    access.geometry = [weak] {
        const auto list = weak.lock();
        return list ? list->scrollbarGeometry() : lib::ItemScrollGeometry{};
    };
    access.current = [weak, expected, token=std::weak_ptr<lib::ItemScrollBarStateMachine>(gesture)] {
        const auto list = weak.lock();
        return list && list->scroll_gesture_ == token.lock() &&
            list->presentationRevision() == *expected;
    };
    access.set_position = [weak, expected](const bool vertical, const float value, const bool relative) {
        const auto list = weak.lock();
        if (!list || !vertical || list->presentationRevision() != *expected) return false;
        const auto before = list->scrollOffset();
        // D版と同様、ページ移動のクリックは表示領域の半分とし、共通の既定値である全体幅・高さは使わない。
        const auto target = relative ? before + (value < 0 ? -list->scroll_.page_increment : list->scroll_.page_increment) : value;
        const auto clamped = std::clamp(target, 0.0F,
            std::max(0.0F, list->scroll_.maximum - list->scroll_.thumb));
        if (clamped == before) return false;
        // layoutTiles は監視側への通知前に、変更番号を一度だけ更新する。
        // 再入による置換・倍率変更・二度目のスクロールは、その操作を無効化する。
        ++*expected;
        list->setScrollOffset(clamped);
        return list->scrollOffset() != before;
    };
    scroll_gesture_ = gesture;
    if (!gesture->begin(self, event.position, std::move(access)) || !gesture->owner() ||
        scroll_gesture_ != gesture || image_revision_ != *expected) {
        if (scroll_gesture_ == gesture) cancelScrollbarInteraction();
        return true;
    }
    const auto gui = gui_;
    try { if (gui) gui->capture_pointer(self, true); }
    catch (...) {
        const auto error = std::current_exception();
        try { if (scroll_gesture_ == gesture) cancelScrollbarInteraction(); } catch (...) {}
        std::rethrow_exception(error);
    }
    if (scroll_gesture_ != gesture) {
        // キャプチャーのフックがギャラリーを閉じて開き直す場合がある。 新しい操作
        // のキャプチャーを解除せず、所有者のない古いキャプチャーだけを解放する。
        if (!scroll_gesture_ && gui) gui->capture_pointer(self, false);
        return true;
    }
    const auto redraw = invalidate;
    if (redraw) redraw();
    return true;
}

bool ImageList::handlePointer(lib::PointerEvent& event) {
    const auto keep_alive = weak_from_this().lock();
    const auto index = indexOf(static_cast<int>(std::lround(event.position.x)),
                               static_cast<int>(std::lround(event.position.y)));
    if (event.action == lib::PointerAction::Move ||
        event.action == lib::PointerAction::Enter) {
        if (index < 0) {
            set_cursor(lib::CursorKind::Default);
            set_tooltip({});
        } else {
            set_cursor(lib::CursorKind::Hand);
            auto tooltip = paths_[static_cast<std::size_t>(index)];
            std::string escaped;
            escaped.reserve(tooltip.size());
            for (const auto character : tooltip) {
                if (character == '&') escaped.push_back('&');
                escaped.push_back(character);
            }
            set_tooltip(std::move(escaped));
        }
        return false;
    }
    if (event.action != lib::PointerAction::Down ||
        event.button != lib::PointerButton::Primary)
        return false;
    event.handled = true;
    if (index >= 0) {
        select(paths_[static_cast<std::size_t>(index)]);
        const auto callback = on_item_activated;
        if (callback) callback(static_cast<std::size_t>(index));
    }
    return true;
}

ImageListWindowBase::ImageListWindowBase(
    const CProps& prop, lib::Owner<Summary> summary, lib::Owner<Skin> force_skin,
    lib::Owner<lib::Window> parent, SelectionCallback selection,
    lib::WidgetPtr base_control, ImageListWindowOptions options,
    const bool card_type)
    : prop_(&prop), summary_owner_(std::move(summary)),
      summary_(summary_owner_.get()), force_skin_(std::move(force_skin)),
      parent_(std::move(parent)), base_control_(std::move(base_control)),
      selection_(std::move(selection)), options_(std::move(options)),
      card_type_(card_type) {
    initialize();
}

ImageListWindowBase::ImageListWindowBase(
    const CProps& prop, Summary& summary, lib::Owner<Skin> force_skin,
    lib::Owner<lib::Window> parent, SelectionCallback selection,
    lib::WidgetPtr base_control, ImageListWindowOptions options,
    const bool card_type)
    : prop_(&prop), summary_(&summary), force_skin_(std::move(force_skin)),
      parent_(std::move(parent)), base_control_(std::move(base_control)),
      selection_(std::move(selection)), options_(std::move(options)),
      card_type_(card_type) {
    initialize();
}

void ImageListWindowBase::initialize() {
    standalone_visibility_ = !lib::editor_window_host_options().all_mode;
    callback_lifetime_->owner = this;
    if (force_skin_) summ_skin_ = force_skin_;
    else if (options_.skin) summ_skin_ = options_.skin;
    else if ((!summary_ || summary_->scenarioPath().empty()) && options_.skin_resolver)
        summ_skin_ = options_.skin_resolver(*prop_, summary_);

    shell_ = std::make_shared<lib::Window>();
    shell_->set_layout(std::make_shared<lib::FillLayout>());
    shell_->resizable = true;
    shell_->minimizable = false;
    shell_->maximizable = false;
    lib::configure_owned_window(shell_, parent_,
        lib::OwnedWindowKind::Modeless);
    shell_->set_visible(false);
    const auto weak = std::weak_ptr<CallbackLifetime>(callback_lifetime_);
    shell_->on_close_requested = [weak] {
        if (const auto life=weak.lock(); life && life->owner) life->owner->close();
    };
    shell_->on_bounds_changed = [weak](const lib::Rect value) {
        const auto life = weak.lock();
        if (!life || !life->owner) return;
        auto* owner = life->owner;
        const auto revision = life->revision;
        owner->layout();
        const auto save = owner->options_.save_window_size;
        if (life->owner == owner && life->revision == revision && save)
            save({value.width, value.height});
    };
    list_ = std::make_shared<ImageList>();
    list_->setGraphics(options_.graphics);
    list_->setGui(options_.gui);
    // Canvas の無効化はこのモデルを更新するが、開いているモードレス
    // のギャラリーには ネイティブのホストへの明示的な再描画要求も必要。
    const std::weak_ptr<lib::GuiBackend> weak_gui = options_.gui;
    const std::weak_ptr<ImageList> weak_list = list_;
    list_->invalidate = [weak, weak_gui, weak_list] {
        const auto life = weak.lock();
        const auto gui = weak_gui.lock();
        const auto list = weak_list.lock();
        if (!life || !life->owner || !gui || !list) return;
        const auto* owner = life->owner;
        if (owner->closed_ || owner->list_ != list) return;
        gui->request_redraw(list, std::nullopt);
    };
    list_->setFontHeight(options_.font_height);
    list_->setCharacterWidth(options_.character_width);
    if (!options_.font.family.empty() || options_.font.points > 0.0F)
        list_->setFont(options_.font);
    list_->init(imageSize().width,imageSize().height,imageFactory(),
                options_.image_scale,options_.drawing_scale);
    list_->mask(card_type_);
    shell_->add(list_);
    list_->on_item_activated = [weak](std::size_t index) {
        if (const auto life=weak.lock(); life && life->owner) life->owner->handleActivation(index);
    };
    list_->on_items_changed = [weak] {
        const auto life=weak.lock();
        if (!life || !life->owner) return;
        const auto revision=life->revision;
        auto* owner=life->owner;
        owner->layout();
        if (life->owner!=owner || life->revision!=revision) return;
        const auto callback=owner->on_images_changed;
        if (callback) callback();
    };
    const auto watch_visibility = [weak](const lib::WidgetPtr& widget) {
        if (!widget) return;
        const auto previous = widget->on_visibility_changed;
        widget->on_visibility_changed = [previous, weak](const bool visible) {
            if (previous) previous(visible);
            const auto life = weak.lock();
            if (!visible && life && life->owner) life->owner->close();
        };
    };
    watch_visibility(base_control_);
    watch_visibility(parent_);
    list_->on_pointer = [weak, widget=std::weak_ptr<ImageList>(list_)](lib::PointerEvent& event) {
        const auto life=weak.lock();
        const auto list=widget.lock();
        if (!life || !life->owner || !list) return;
        const auto revision=life->revision;
        if (list->handleScrollbarPointer(event)) return;
        const auto index=list->indexOf(static_cast<int>(std::lround(event.position.x)),
                                       static_cast<int>(std::lround(event.position.y)));
        (void)list->handlePointer(event);
        // 有効な項目は handleActivation 経由で閉じる。二重に閉
        // じたり、 選択・終了通知によって新しく開かれたものを閉じたりしない。
        if (index<0 && life->owner && life->revision==revision &&
            event.action==lib::PointerAction::Down && event.button==lib::PointerButton::Primary)
            life->owner->close();
    };
    if (options_.window_size.width>0 || options_.window_size.height>0)
        setWindowBounds({0,0,options_.window_size.width,options_.window_size.height});
    else {
        const auto image=imageSize();
        setWindowBounds({0,0,image.width+ImageList::SPACING*3.0F,
                         std::max(240.0F,image.height*3.0F)});
    }
    bindDragDrop();
}

ImageList::ImageFactory ImageListWindowBase::imageFactory() const {
    const auto weak=std::weak_ptr<CallbackLifetime>(callback_lifetime_);
    return [weak](lib::StringView path,bool mask) {
        const auto life=weak.lock();
        return life && life->owner ? life->owner->createImage(path,mask) : ImageDataWithScale{};
    };
}

ImageListWindowBase::~ImageListWindowBase() {
    callback_lifetime_->owner = nullptr;
    ++callback_lifetime_->revision;
    closed_ = true;
    const auto shell = shell_;
    const auto gui = options_.gui;
    if (shell) { shell->set_visible(false); shell->on_close_requested = {}; }
    if (list_) {
        try { list_->cancelScrollbarInteraction(); } catch (...) {}
        list_->on_pointer = {};
        list_->on_item_activated = {};
        list_->on_items_changed = {};
        // 実行中の生成・アップロード処理には、画像の世代が変わったことが見える。 ネイティブ側や
        // フレームのスナップショットでは、Canvas がラッパーより長く存続する場合がある。
        try { list_->removeAll(); list_->init(0,0,{}); } catch (...) {}
    }
    unbindDragDrop();
    try { if (gui && shell) gui->close(shell); } catch (...) {}
}

bool ImageListWindowBase::hostEffectivelyVisible() const {
    if ((base_control_ && !base_control_->visible()) || (parent_ && !parent_->visible())) return false;
    if (!standalone_visibility_ || !parent_ || !base_control_) return true;
    std::vector<const lib::Widget*> path;
    const auto state = inspectHostVisibility(parent_, base_control_.get(), true, path, nullptr);
    // 組み込み側が切り離された基底コントロールを渡す場合がある。 指定された所有者から
    // そのコントロールへの経路がなければ、直接の表示状態に従う従来の扱いを維持する。
    return !state.found || state.visible;
}

void ImageListWindowBase::watchHostVisibility() {
    if (!standalone_visibility_ || !parent_ || !base_control_) return;
    std::vector<const lib::Widget*> path;
    std::vector<lib::WidgetPtr> ancestors;
    (void)inspectHostVisibility(parent_, base_control_.get(), true, path, &ancestors);
    const auto weak = std::weak_ptr<CallbackLifetime>(callback_lifetime_);
    for (const auto& widget : ancestors) {
        std::erase_if(visibility_observers_, [](const auto& value) { return value.expired(); });
        if (std::any_of(visibility_observers_.begin(), visibility_observers_.end(),
            [&](const auto& value) { return value.lock() == widget; })) continue;
        visibility_observers_.push_back(widget);
        // 基底コントロールと所有者には、initialize で直接の表示状態リスナ
        // ーを登録済み。 それ以外の祖先については、実際のツリーに基づく確認が必要。
        if (widget != base_control_ && widget != parent_) {
            const auto previous = widget->on_visibility_changed;
            widget->on_visibility_changed = [previous, weak](bool visible) {
                const auto before = weak.lock();
                const auto revision = before ? before->revision : 0;
                if (previous) previous(visible);
                const auto life = weak.lock();
                if (!life || !life->owner || life->revision != revision || life->owner->closed_) return;
                if (!life->owner->hostEffectivelyVisible()) life->owner->close();
            };
        }
        if (const auto tabs = std::dynamic_pointer_cast<lib::TabView>(widget)) {
            const auto previous = tabs->on_selection_changed;
            tabs->on_selection_changed = [previous, weak](std::optional<std::size_t> index) {
                const auto before = weak.lock();
                const auto revision = before ? before->revision : 0;
                if (previous) previous(index);
                const auto life = weak.lock();
                if (!life || !life->owner || life->revision != revision || life->owner->closed_) return;
                if (!life->owner->hostEffectivelyVisible()) life->owner->close();
            };
        }
    }
}

void ImageListWindowBase::open() {
    watchHostVisibility();
    if (!hostEffectivelyVisible()) {
        close();
        return;
    }
    const auto life = callback_lifetime_;
    const auto revision = closed_ ? ++life->revision : life->revision;
    const auto shell = shell_;
    const auto list = list_;
    const auto gui = options_.gui;
    closed_ = false;
    shell->set_visible(true);
    try {
        bindDragDrop();
        if (life->owner != this || life->revision != revision) return;
        layout();
        if (life->owner != this || life->revision != revision) return;
        list->showSelection();
        if (life->owner != this || life->revision != revision) return;
        if (gui) gui->show(shell);
    } catch (...) {
        const auto error = std::current_exception();
        // 新しい開閉処理が現在の状態を所有する。この失敗した試行だけを無効
        // 化し、 後処理の失敗で元の提供処理のエラーを隠さないようにする。
        if (life->owner == this && life->revision == revision) {
            try { close(); } catch (...) {}
        }
        std::rethrow_exception(error);
    }
}

void ImageListWindowBase::close() {
    if (closed_) return;
    const auto life = callback_lifetime_;
    const auto revision = ++life->revision;
    const auto shell = shell_;
    const auto list = list_;
    const auto gui = options_.gui;
    const auto notify = on_closed;
    closed_ = true;
    std::exception_ptr error;
    const auto current = [&] { return life->owner == this && life->revision == revision; };
    const auto retired = [&] {
        if (current()) return false;
        if (error) std::rethrow_exception(error);
        return true;
    };
    try { list->cancelScrollbarInteraction(); } catch (...) { error = std::current_exception(); }
    if (retired()) return;
    try { if (shell) shell->set_visible(false); } catch (...) { if (!error) error = std::current_exception(); }
    if (retired()) return;
    list->invalidatePendingPaint();
    try { unbindDragDrop(); } catch (...) { if (!error) error = std::current_exception(); }
    if (retired()) return;
    try { if (gui && shell) gui->close(shell); } catch (...) { if (!error) error = std::current_exception(); }
    if (current()) {
        try { if (notify) notify(); } catch (...) { if (!error) error = std::current_exception(); }
    }
    if (error) std::rethrow_exception(error);
}

void ImageListWindowBase::images(lib::String dir,
                                 lib::Array<lib::String> paths) {
    const auto list = list_;
    shell_->title = std::move(dir);
    list->replace(std::move(paths));
}

void ImageListWindowBase::select(const lib::StringView path_value) {
    const auto life = callback_lifetime_;
    const auto revision = life->revision;
    const auto list = list_;
    list->select(path_value);
    if (life->owner == this && life->revision == revision) list->showSelection();
}

void ImageListWindowBase::mask(const bool value) {
    const auto list = list_;
    list->mask(value);
}

bool ImageListWindowBase::mask() const noexcept {
    return list_->mask();
}

void ImageListWindowBase::refImageScale(const lib::Size image_size,
                                         const float image_scale,
                                         const int drawing_scale) {
    if (card_type_) options_.card_image_size = image_size;
    else options_.background_image_size = image_size;
    options_.image_scale = image_scale;
    options_.drawing_scale = drawing_scale;
    refImageScale();
}

void ImageListWindowBase::refImageScale() {
    const auto life = callback_lifetime_;
    const auto revision = life->revision;
    const auto list = list_;
    const auto is_card = card_type_;
    const auto size = imageSize();
    list->init(size.width, size.height, imageFactory(),
                options_.image_scale, options_.drawing_scale);
    if (life->owner != this || life->revision != revision) return;
    list->mask(is_card);
    if (life->owner == this && life->revision == revision) layout();
}

ImageDataWithScale ImageListWindowBase::createImage(
    const lib::StringView path_value, const bool mask_value) const {
    const auto life = callback_lifetime_;
    if (!life->owner) return {};
    const auto revision = life->revision;
    const auto summary_owner = summary_owner_;
    const auto factory = options_.image_factory;
    const auto resource = options_.image_resource;
    const auto version = summary_ ? summary_->dataVersion()
                                  : ::cwxeditor::cwx::versioninfo::LATEST_VERSION;
    const auto scenario_path = summary_ ? summary_->scenarioPath() : lib::String{};
    const auto skin = summSkin();
    // summSkin は呼び出し元が指定した所有者の解決処理を呼ぶ。 このラッ
    // パーが無効化される場合があるため、取得済みの要求で続行する前に再検証する。
    if (!life->owner || life->revision != revision) return {};
    auto resolved = lib::String(path_value);
    if (skin && !path_value.empty()) {
        bool skin_material = false;
        bool engine_material = false;
        std::size_t default_index = 0U;
        resolved = skin->findPathF(
            path_value, skin->extImage(), skin->tableDirs(), scenario_path, version,
            skin->wsnTableDirs(version), skin_material, engine_material,
            default_index);
        if (resolved.empty()) resolved = lib::String(path_value);
    }
    if (life->owner != this || life->revision != revision) return {};
    // 提供処理の中でラッパーが破棄されても、結果の所有権は ImageList
    // に返す。 一覧の世代チェックによって解放する。
    if (factory) return factory(resolved, mask_value);

    ImageDataWithScale result;
    if (resource && !resolved.empty()) {
        result.resource = resource(resolved, mask_value);
        result.loaded = true;
    }
    return result;
}

lib::Array<lib::String> ImageListWindowBase::defExts() const {
    const auto skin = summSkin();
    return skin ? skin->extImage() : lib::Array<lib::String>{};
}

lib::Array<lib::String> ImageListWindowBase::defDirs() const {
    const auto skin = summSkin();
    return skin ? skin->tableDirs() : lib::Array<lib::String>{};
}

lib::Size ImageListWindowBase::imageSize() const noexcept {
    return card_type_ ? options_.card_image_size : options_.background_image_size;
}

void ImageListWindowBase::setWindowBounds(const lib::Rect value) {
    shell_->set_bounds(value);
}

lib::Rect ImageListWindowBase::windowBounds() const noexcept {
    return shell_->bounds();
}

void ImageListWindowBase::layout() {
    if (!shell_ || !list_) return;
    const auto list = list_;
    const auto value = shell_->bounds();
    list->set_bounds({0.0F, 0.0F, value.width, value.height});
    list->setViewport({value.width, value.height});
}

bool ImageListWindowBase::addPath(lib::String path_value) {
    return list_->insert(list_->paths().size(), std::move(path_value));
}

bool ImageListWindowBase::removePath(const std::size_t index) {
    return list_->removeAt(index);
}

bool ImageListWindowBase::movePath(const std::size_t from,
                                   const std::size_t to) {
    return list_->move(from, to);
}

const lib::String& ImageListWindowBase::title() const noexcept {
    return shell_->title;
}

bool ImageListWindowBase::hasOperation(const lib::DropOperation value,
                                       const lib::DropOperation bit) const noexcept {
    return ::cwxeditor::cwx::editor::gui::dwt::imagelistwindow::hasBit(value, bit);
}

lib::ClipboardData ImageListWindowBase::dragData() const {
    lib::ClipboardData result;
    const auto path_value = list_->selectedPath();
    if (path_value.empty()) return result;
    result.text = path_value;
    result.files.emplace_back(::cwxeditor::cwx::structs::pathFromUtf8(path_value));
    return result;
}

lib::Array<lib::String> ImageListWindowBase::extractDroppedPaths(
    const lib::DragEvent& event) const {
    const auto provider = options_.dropped_paths;
    if (provider) return provider(event);
    lib::Array<lib::String> result;
    for (const auto& path_value : event.data.files) {
        auto value = fromPath(path_value);
        if (!value.empty()) result.push_back(std::move(value));
    }
    if (!result.empty()) return result;
    std::size_t start = 0U;
    while (start <= event.data.text.size()) {
        const auto end = event.data.text.find('\n', start);
        auto value = event.data.text.substr(
            start, end == lib::String::npos ? lib::String::npos : end - start);
        value = fileUriToPath(std::move(value));
        if (!value.empty()) result.push_back(std::move(value));
        if (end == lib::String::npos) break;
        start = end + 1U;
    }
    return result;
}

bool ImageListWindowBase::reorderDropped(lib::Array<lib::String> paths,
                                         std::size_t target,
                                         const lib::DropOperation operation) {
    const auto list=list_;
    auto next=list->paths();
    const auto selected=list->selectedPath();
    target=std::min(target,next.size());
    for (auto& path : paths) {
        const auto found=std::find(next.begin(),next.end(),path);
        if (operation==lib::DropOperation::Move && found!=next.end()) {
            const auto from=static_cast<std::size_t>(found-next.begin());
            if (from<target) --target;
            next.erase(found);
        }
        target=std::min(target,next.size());
        next.insert(next.begin()+static_cast<std::ptrdiff_t>(target),std::move(path));
        ++target;
    }
    if (next==list->paths()) return false;
    // 完成した並び順を一度に反映する。複数ファイルのドロップ中に、 フ
    // ァイルごとのコールバックが一覧を途中で置き換えないようにする。
    list->replace(std::move(next),selected,true);
    return true;
}

bool ImageListWindowBase::handleDrop(lib::DragEvent& event) {
    if (closed_) return false;
    const auto life=callback_lifetime_;
    const auto revision=life->revision;
    const auto list=list_;
    const auto list_revision=list->presentationRevision();
    const auto allowed=options_.drag_operations;
    const auto notify=on_drag_drop;
    const auto paths=extractDroppedPaths(event);
    if (life->owner!=this || life->revision!=revision ||
        list->presentationRevision()!=list_revision || paths.empty())
        return false;
    const auto operation=lib::DragDropStateMachine::choose(
        allowed,event.allowed==lib::DropOperation::None ? allowed : event.allowed,event.selected);
    if (operation==lib::DropOperation::None) return false;
    const auto hit=list->indexOf(static_cast<int>(std::lround(event.position.x)),
                                static_cast<int>(std::lround(event.position.y)));
    const auto target=event.target_index.value_or(hit<0 ? list->paths().size() :
                                                  static_cast<std::size_t>(hit));
    if (!reorderDropped(paths,target,operation)) return false;
    event.selected=operation;
    event.handled=true;
    if (life->owner==this && life->revision==revision && notify) notify(event);
    return true;
}

void ImageListWindowBase::bindDragDrop() {
    const auto life=callback_lifetime_;
    if (life->binding || closed_ || drag_bound_ || !options_.enable_drag_drop ||
        !options_.drag_drop || !list_) return;
    const auto revision=life->revision;
    const auto backend=options_.drag_drop;
    const auto list=list_;
    const auto operations=options_.drag_operations;
    const auto formats=options_.accepted_formats;
    const auto weak=std::weak_ptr<CallbackLifetime>(life);
    const auto current=[&] {
        return life->owner==this && life->revision==revision && !closed_;
    };
    const auto negotiate=[weak,operations,revision](lib::DragEvent& event) {
        const auto token=weak.lock();
        if (!token || !token->owner || token->revision!=revision || token->owner->closed_) {
            event.selected=lib::DropOperation::None;
            return;
        }
        if (event.allowed==lib::DropOperation::None) event.allowed=operations;
        event.selected=lib::DragDropStateMachine::choose(operations,event.allowed,event.selected);
    };
    life->binding=true;
    drag_bound_=true;
    std::exception_ptr error;
    try {
        backend->bind_source({list,operations,[weak,revision] {
            const auto token=weak.lock();
            return token && token->owner && token->revision==revision && !token->owner->closed_ ?
                token->owner->dragData() : lib::ClipboardData{};
        },[](lib::DropOperation){}});
        if (current()) backend->bind_target({list,formats,negotiate,negotiate,
            [weak,revision](lib::DragEvent& event) {
                const auto token=weak.lock();
                if (token && token->owner && token->revision==revision && !token->owner->closed_)
                    (void)token->owner->handleDrop(event);
            },{}});
    } catch (...) { error=std::current_exception(); }
    const bool committed=!error && current();
    if (!committed) {
        // バックエンドは、コールバックを呼んだ後で登録する場合がある。 保留中の
        // 再表示が同じウィジェットに接続する前に、その遅れた登録を無効化する。
        try { backend->unbind(list); } catch (...) { if (!error) error=std::current_exception(); }
    }
    if (life->owner==this) drag_bound_=committed;
    life->binding=false;
    if (error) std::rethrow_exception(error);
    if (!committed && life->owner && !life->owner->closed_) life->owner->bindDragDrop();
}

void ImageListWindowBase::unbindDragDrop() noexcept {
    const auto life=callback_lifetime_;
    if (!drag_bound_ || !options_.drag_drop || !list_) return;
    const auto backend=options_.drag_drop;
    const auto list=list_;
    drag_bound_=false;
    // 実行中の接続処理が、戻る前に途中までの接続や遅れて行われた接続を無効化する。
    if (life->binding) return;
    life->binding=true;
    try { backend->unbind(list); } catch (...) {}
    life->binding=false;
    if (life->owner && !life->owner->closed_) {
        try { life->owner->bindDragDrop(); } catch (...) {}
    }
}

void ImageListWindowBase::handleActivation(const std::size_t index) {
    const auto life = callback_lifetime_;
    const auto revision = life->revision;
    const auto selection = selection_;
    const auto selected = on_selected;
    if (index >= list_->paths().size()) return;
    const auto value = list_->path(index);
    if (selection) selection(value);
    if (life->owner != this || life->revision != revision) return;
    if (selected) selected(value);
    if (life->owner == this && life->revision == revision) close();
}

template class ImageListWindow<ImageListType::CARD>;
template class ImageListWindow<ImageListType::BG_IMG>;

}  // namespace cwxeditor::cwx::editor::gui::dwt::imagelistwindow
