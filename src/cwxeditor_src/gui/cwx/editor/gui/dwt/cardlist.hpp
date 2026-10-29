#pragma once

#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/drag_drop.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/graphics.hpp"
#include "cwxeditor_src/lib/services.hpp"
#include "cwxeditor_src/lib/ui_metrics.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::cardlist {

namespace lib = ::cwxeditor::lib;

using Point = lib::Point;
using Rectangle = lib::Rect;
using Size = lib::Size;
using ImageData = lib::ImageData;
using Canvas = lib::Canvas;
using GraphicsContext = lib::DrawingContext;
using Services = lib::Services;
using Card = ::cwxeditor::cwx::card::Card;
using CardPtr = std::shared_ptr<Card>;

struct VerticalScrollbarGeometry {
    Rectangle track{};
    Rectangle thumb{};
};

/// CardListが受け付けるスタイルビット。ホ
/// ストGUIの定数を公開せず、動作を指定する。
enum Style : int {
    None = 0,
    Multi = 1 << 0,
    HorizontalScroll = 1 << 1,
    VerticalScroll = 1 << 2,
    Virtual = 1 << 3,
};

struct SelectionEvent {
    std::vector<int> indices;
    int selection{-1};
    lib::Modifiers modifiers{lib::Modifiers::None};
    bool handled{};
};

[[nodiscard]] constexpr bool hasModifier(lib::Modifiers value,
                                         lib::Modifiers modifier) noexcept {
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(modifier)) != 0U;
}

using TextMeasure = std::function<Size(std::string_view)>;
[[nodiscard]] std::string cutText(std::string_view name, TextMeasure measure_text,
                                  int max_width, std::string_view dots = "...",
                                  bool include_last_char = false);
[[nodiscard]] std::string cutText(std::string_view name, GraphicsContext& context,
                                  int max_width, std::string_view dots = "...",
                                  bool include_last_char = false,
                                  lib::FontSpec font = {});

template <typename C>
class CardList;

template <typename C>
class CardListItem {
public:
    using Parent = CardList<C>;
    using ImageCreator = std::function<ImageData(const C&)>;
    using TitleCreator = std::function<std::string(const C&)>;

    CardListItem(Parent& parent, C value, ImageCreator create_image,
                 TitleCreator create_title)
        : parent_(&parent), data_(std::move(value)),
          create_image_(std::move(create_image)),
          create_title_(std::move(create_title)) {
        createTitle();
    }

    CardListItem(const CardListItem&) = delete;
    CardListItem& operator=(const CardListItem&) = delete;
    ~CardListItem() { dispose(); }

    [[nodiscard]] const C& data() const noexcept { return data_; }
    [[nodiscard]] C& data() noexcept { return data_; }
    void setData(C value) {
        disposeImage();
        data_ = std::move(value);
        createTitle();
    }

    void createImage(bool force = false) {
        if (!create_image_ || (!force && image_created_)) return;
        const auto request = ++image_revision_;
        const auto provider = create_image_;
        const auto value = data_;
        const auto graphics = parent_ ? parent_->services().graphics : nullptr;
        auto next = provider(value);
        if (request != image_revision_) return;
        lib::ResourceId resource{};
        bool created_resource{};
        if (graphics && !next.frames.empty()) {
            if (image_resource_ != 0U && image_graphics_ == graphics) {
                graphics->update_image(image_resource_, next);
                resource = image_resource_;
            } else {
                resource = graphics->create_image(next);
                created_resource = resource != 0U;
            }
        }
        if (request != image_revision_) {
            if (created_resource && graphics) graphics->destroy_image(resource);
            return;
        }
        const auto old_graphics = std::move(image_graphics_);
        const auto old_resource = image_resource_;
        image_data_ = std::move(next);
        image_resource_ = resource;
        image_graphics_ = graphics;
        image_created_ = true;
        if (old_resource != 0U &&
            (old_resource != resource || old_graphics != graphics) && old_graphics)
            old_graphics->destroy_image(old_resource);
    }

    void createTitle() {
        title_ = create_title_ ? create_title_(data_) : std::string{};
    }

    [[nodiscard]] int x() const noexcept { return x_; }
    void x(int value) noexcept { x_ = value; }
    [[nodiscard]] int y() const noexcept { return y_; }
    void y(int value) noexcept { y_ = value; }

    [[nodiscard]] int width() const {
        const auto size = imageSize();
        return std::max(0, static_cast<int>(std::lround(size.width)));
    }
    [[nodiscard]] int height() const {
        const auto size = imageSize();
        const auto image_height = std::max(0, static_cast<int>(std::lround(size.height)));
        if (!create_title_) return image_height;
        return image_height + parent_->titleSpace() + parent_->fontHeight() + 1;
    }

    [[nodiscard]] Rectangle imageBounds() const {
        const auto size = imageSize();
        return {static_cast<float>(x_), static_cast<float>(y_), size.width, size.height};
    }

    [[nodiscard]] Rectangle titleBounds() const {
        if (!create_title_ || title_.empty() || parent_ == nullptr) return {};
        // 当たり判定には、実際に描画する省略記号付きタイトルと同じ文字列の寸法を使う。
        const auto visible_title = ::cwxeditor::cwx::editor::gui::dwt::cardlist::cutText(
            title_, [this](std::string_view value) { return parent_->measureText(value); },
            width());
        const auto text_size = parent_->measureText(visible_title);
        const auto image = imageSize();
        return {static_cast<float>(x_) + (static_cast<float>(width()) - text_size.width) / 2.0F,
                static_cast<float>(y_) + image.height + parent_->titleSpace() -
                    static_cast<float>(parent_->titleGlyphOffset()),
                text_size.width, text_size.height};
    }

    [[nodiscard]] std::string cutText(GraphicsContext& context) const {
        // Dと同じく、各タイトルの計測と描画で同じ部品のフォントを使う。
        return ::cwxeditor::cwx::editor::gui::dwt::cardlist::cutText(
            title_, context, width(), "...", false, parent_->fontSpec());
    }

    [[nodiscard]] lib::ResourceId imageResource() const noexcept { return image_resource_; }
    [[nodiscard]] const ImageData& imageData() const noexcept { return image_data_; }
    [[nodiscard]] const std::string& title() const noexcept { return title_; }
    [[nodiscard]] bool hasTitleCreator() const noexcept { return static_cast<bool>(create_title_); }

    void disposeImage() noexcept {
        ++image_revision_;
        const auto graphics = std::move(image_graphics_);
        const auto resource = image_resource_;
        image_resource_ = 0U;
        image_data_ = {};
        image_created_ = false;
        if (resource != 0U && graphics) graphics->destroy_image(resource);
    }

    void dispose() noexcept { disposeImage(); }

private:
    [[nodiscard]] Size imageSize() const {
        if (!image_created_ && create_image_) const_cast<CardListItem*>(this)->createImage();
        if (image_data_.frames.empty()) return {};
        const auto& frame = image_data_.frames.front();
        if (frame.logical_size.width > 0.0F && frame.logical_size.height > 0.0F)
            return frame.logical_size;
        return {static_cast<float>(frame.pixel_width), static_cast<float>(frame.pixel_height)};
    }

    Parent* parent_{};
    C data_{};
    ImageCreator create_image_{};
    TitleCreator create_title_{};
    ImageData image_data_{};
    lib::ResourceId image_resource_{};
    std::shared_ptr<lib::GraphicsBackend> image_graphics_{};
    std::uint64_t image_revision_{};
    std::string title_{};
    int x_{};
    int y_{};
    bool image_created_{};
};

template <typename C>
class CardListDragSourceEffect {
public:
    explicit CardListDragSourceEffect(CardList<C>& list) noexcept : list_(&list) {}

    [[nodiscard]] CardList<C>* list() const noexcept { return list_; }
    void disposeImage() noexcept { drag_image_ = {}; }
    void dragFinished(const lib::DragEvent&) noexcept { disposeImage(); }
    void dragStart(lib::DragEvent& event);

    [[nodiscard]] const ImageData& dragImage() const noexcept { return drag_image_; }

private:
    CardList<C>* list_{};
    ImageData drag_image_{};
};

template <typename C>
class CardList final : public lib::Canvas {
public:
    using Value = C;
    using Item = CardListItem<C>;
    using ItemPtr = std::shared_ptr<Item>;
    using ParentPtr = std::shared_ptr<lib::Container>;
    using ImageCreator = std::function<ImageData(const C&)>;
    using TitleCreator = std::function<std::string(const C&)>;
    using ToolTipCreator = std::function<std::string(const C&)>;
    using AdditionalPaint = std::function<void(Canvas&, GraphicsContext&, const C&, Rectangle)>;
    using SelectionListener = std::function<void(const SelectionEvent&)>;

    // 準備した行は値と画像を所有し、準備中に表示中の一覧を
    // 消さない。 公開時は提供元・選択・再描画を呼ばず、親
    // ペインが表と画像一覧をまとめて公開できるようにする。
    class PreparedRefresh {
        friend class CardList;
    public:
        PreparedRefresh(PreparedRefresh&&) = default;
        PreparedRefresh& operator=(PreparedRefresh&&) = default;
        PreparedRefresh(const PreparedRefresh&) = delete;
        PreparedRefresh& operator=(const PreparedRefresh&) = delete;
    private:
        PreparedRefresh() = default;
        const CardList* owner{};
        std::weak_ptr<int> lifetime;
        std::uint64_t revision{};
        std::uint64_t selection_revision{};
        std::vector<ItemPtr> items;
        std::set<int> selected;
        ImageCreator image_creator;
        TitleCreator title_creator;
        int width{}, height{}, cursor{-1};
    };

    CardList(ParentPtr parent = {}, int style = 0, Services services = {})
        : parent_(std::move(parent)), style_(style), services_(std::move(services)) {
        refreshTitleTypography();
        set_cursor(lib::CursorKind::Arrow);
        // DWTのカードペインと同じく白いクライアント領
        // 域を使う。 部分描画でも既知の色で以前の選択
        // 枠・タイトルを消してから、新しい状態を描く。
        set_background(lib::Color{255U, 255U, 255U, 255U});
        on_paint = [this](Rectangle) { ++paint_revision_; };
        on_paint_context = [this](GraphicsContext& context, Rectangle dirty) {
            ++paint_revision_;
            repaint(context, dirty);
        };
        on_pointer = [this](lib::PointerEvent& event) { handlePointer(event); };
        on_key_down = [this](lib::KeyEvent& event) { handleKeyDown(event); };
        on_key_up = [this](lib::KeyEvent& event) { handleKeyUp(event); };
        // カードグリッドはScrollViewではなく
        // 独自Canvasなので、ホイールで自身の表示
        // 原点を動かす。 1段階をカード1行分とし、縦
        // にはみ出していない場合は横へスクロールする。
        on_wheel = [this](lib::WheelEvent& event) {
            const auto content = contentSize();
            const bool vertical_overflow =
                content.height > bounds().height + 0.5F;
            const float delta = event.delta_y != 0.0F ? event.delta_y : event.delta_x;
            if (delta == 0.0F) return;
            if (vertical_overflow) {
                const int step = std::max(1, item_height_ + space_y_);
                const auto before = origin_.y;
                scrollY(static_cast<int>(std::lround(origin_.y - delta * static_cast<float>(step))));
                event.handled = origin_.y != before;
            } else {
                const int step = std::max(1, item_width_ + space_x_);
                const auto before = origin_.x;
                scrollX(static_cast<int>(std::lround(origin_.x - delta * static_cast<float>(step))));
                event.handled = origin_.x != before;
            }
        };
        on_focus = [this] {
            if (!multi() && selected_.empty() && !items_.empty()) {
                select(0);
                callSelectChanged();
            }
            redrawCard(cursor_);
        };
        on_blur = [this] {
            redrawCard(cursor_);
            stopScrollbarDrag();
            mouseRelease();
        };
        bindDragSource();
    }

    CardList(const CardList&) = delete;
    CardList& operator=(const CardList&) = delete;

    ~CardList() override {
        timer_lifetime_.reset();
        mouseRelease();
        if (services_.drag_drop) services_.drag_drop->unbind(widgetHandle());
        disposeItems();
    }

    [[nodiscard]] lib::ComponentKind kind() const noexcept override {
        return lib::ComponentKind::Canvas;
    }
    [[nodiscard]] bool supports_item_view_text_input() const noexcept override {
        return true;
    }
    [[nodiscard]] std::optional<lib::Rect> item_view_text_input_anchor(
        float) const override {
        // CardListはCanvasで描く仮想化された縮小画像一
        // 覧。 Canvasの下端ではなく、現在の主選択セルを使う。
        if (!visible() || !enabled() ||
            static_cast<int>(std::max(0.0F, bounds().width)) != layout_width_ ||
            static_cast<int>(std::max(0.0F, bounds().height)) != layout_height_ ||
            lib::editor_window_host_options().all_mode != layout_all_mode_)
            return std::nullopt;
        const auto index = selection();
        if (index < 0 || index >= count() || wrap_ <= 0) return std::nullopt;
        const auto cell = getBounds(index);
        const auto viewport = clientRectangle();
        const float left = std::max(cell.x, viewport.x) + 1.0F;
        const float top = std::max(cell.y, viewport.y) + 1.0F;
        const float right = std::min(cell.x + cell.width, viewport.x + viewport.width) - 1.0F;
        const float bottom = std::min(cell.y + cell.height, viewport.y + viewport.height) - 1.0F;
        if (right <= left || bottom <= top) return std::nullopt;
        return lib::Rect{bounds().x + left, bounds().y + top,
                         right - left, bottom - top};
    }

    [[nodiscard]] ParentPtr parent() const noexcept { return parent_.lock(); }
    [[nodiscard]] int style() const noexcept { return style_; }
    [[nodiscard]] bool multi() const noexcept { return (style_ & Style::Multi) != 0; }
    [[nodiscard]] const Services& services() const noexcept { return services_; }
    void setServices(Services value) {
        ++refresh_revision_;
        if (services_.drag_drop) services_.drag_drop->unbind(widgetHandle());
        const auto old_items = items_;
        for (const auto& item : old_items) item->disposeImage();
        services_ = std::move(value);
        refreshTitleTypography();
        bindDragSource();
        redraw();
    }

    // CardPaneなどの親部品は、独自の型付きXML転送を
    // 提供する。 その接続前に汎用の添字・タイトルの操作元を抑
    // 止し、別の操作元が親の転送契約を置き換えないようにする。
    void setDefaultDragSourceEnabled(bool enabled) {
        if (default_drag_source_enabled_ == enabled) return;
        default_drag_source_enabled_ = enabled;
        if (!services_.drag_drop) return;
        services_.drag_drop->unbind(widgetHandle());
        if (default_drag_source_enabled_) bindDragSource();
    }

    /// 所有権を持たないハンドルで、サービス側が部品を識別できる
    /// ようにする。 呼び出し元へ寿命の管理方法を強制しない。
    [[nodiscard]] std::shared_ptr<lib::Widget> widgetHandle() const noexcept {
        return {const_cast<CardList*>(this), [](lib::Widget*) {}};
    }

    [[nodiscard]] std::uint64_t redrawRevision() const noexcept { return redraw_revision_; }
    void redraw() noexcept {
        ++redraw_revision_;
        if (services_.gui) services_.gui->request_redraw(widgetHandle());
        if ((!services_.gui || services_.gui->platform() == lib::Platform::Headless) && on_paint)
            on_paint(bounds());
    }
    void redraw(Rectangle dirty) noexcept {
        ++redraw_revision_;
        if (services_.gui) services_.gui->request_redraw(widgetHandle(), dirty);
        if ((!services_.gui || services_.gui->platform() == lib::Platform::Headless) && on_paint)
            on_paint(dirty);
    }

    std::vector<AdditionalPaint> additionalPaint;
    std::function<void(const std::vector<int>&)> on_selection_changed;

    void startRangeSelection(Point view_position) {
        if (!multi() || items_.empty()) return;
        ++range_generation_;
        range_pointer_pos_ = view_position;
        start_pos_ = contentPoint(*range_pointer_pos_);
        end_pos_ = start_pos_;
        if (!ctrl_ && !shift_) deselectAll();
        start_selected_.clear();
        for (const auto index : selected_) start_selected_.insert(index);
        in_ranges_.clear();
        timer_running_ = true;
        frame_ = 0U;
        last_vertical_frame_ = 0U;
        last_horizontal_frame_ = 0U;
        scheduleRangeAutoScroll();
    }

    [[nodiscard]] std::uint64_t addSelectionListener(SelectionListener listener) {
        if (!listener) return 0U;
        const auto token = next_listener_token_++;
        listeners_.push_back({token, std::move(listener)});
        return token;
    }
    void removeSelectionListener(std::uint64_t token) noexcept {
        listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(),
                                        [token](const ListenerEntry& value) {
                                            return value.token == token;
                                        }), listeners_.end());
    }

    void callSelectChanged() {
        SelectionEvent event;
        event.indices = selectionIndices();
        event.selection = selection();
        event.modifiers = modifiers_;
        // 安全な走査にはスナップショットを使うが、DWTと同じく、 自
        // 分の順番が来る前に外されたリスナーは今回の通知でも呼ばない。
        const auto listeners = listeners_;
        for (const auto& entry : listeners) {
            const auto current = std::find_if(listeners_.begin(), listeners_.end(),
                [&entry](const ListenerEntry& value) { return value.token == entry.token; });
            if (current == listeners_.end()) continue;
            const auto callback = current->callback;
            if (callback) callback(event);
        }
        if (on_selection_changed) on_selection_changed(event.indices);
    }

    void setCursor(int index, bool scroll = false, bool select_value = true) {
        if (index < -1 || index >= count()) index = -1;
        if (cursor_ != index) {
            ++selection_revision_;
            redrawCard(cursor_);
            redrawCard(index);
            cursor_ = index;
        }
        if (select_value && cursor_ >= 0) {
            select(cursor_);
            callSelectChanged();
        }
        if (scroll) this->scroll(cursor_);
    }
    [[nodiscard]] int cursor() const noexcept { return cursor_; }

    void redrawCard(int index) {
        if (index < 0 || index >= count()) return;
        redraw(getBounds(index));
    }

    [[nodiscard]] int indexOf(const C& value) const {
        for (std::size_t index = 0; index < items_.size(); ++index) {
            if (sameValue(items_[index]->data(), value)) return static_cast<int>(index);
        }
        return -1;
    }
    void selectValue(const C& value) {
        const auto index = indexOf(value);
        if (index >= 0) select(index);
    }
    template <typename Value = C>
    void select(Value&& value)
        requires (!std::is_same_v<std::remove_cvref_t<Value>, int>) {
        selectValue(static_cast<const C&>(value));
    }
    void select(int index) {
        if (index < 0 || index >= count()) return;
        if (!multi()) {
            if (selected_.size() == 1U && selected_.contains(index)) {
                cursor_ = index;
                return;
            }
            const auto old = selected_;
            ++selection_revision_;
            selected_.clear();
            for (const auto old_index : old) redrawCard(old_index);
            selected_.insert(index);
            cursor_ = index;
            redraw();
            return;
        }
        if (selected_.insert(index).second) {
            ++selection_revision_;
            redrawCard(index);
        }
    }
    void deselect(int index) {
        if (selected_.erase(index) != 0U) {
            ++selection_revision_;
            redrawCard(index);
        }
    }
    void deselectAll() {
        const auto old = selected_;
        if (!old.empty()) ++selection_revision_;
        selected_.clear();
        for (const auto index : old) redrawCard(index);
    }

    [[nodiscard]] std::optional<PreparedRefresh> prepareRefresh(
        const std::vector<C>& values, ImageCreator create_image, TitleCreator create_title,
        std::optional<std::vector<int>> selection = std::nullopt,
        std::optional<std::vector<bool>> replace_images = std::nullopt,
        bool prepare_images = true) {
        PreparedRefresh next;
        next.owner = this;
        next.lifetime = timer_lifetime_;
        next.revision = ++refresh_revision_;
        next.selection_revision = selection_revision_;
        next.image_creator = std::move(create_image);
        next.title_creator = std::move(create_title);
        const auto source = values;
        const auto old_values = selectionCards();
        std::pair<int, int> extents{
            std::max(0, default_item_width_), std::max(0, default_item_height_)};
        next.width = extents.first;
        next.height = extents.second;
        next.items.reserve(source.size());
        for (std::size_t index = 0; index < source.size(); ++index) {
            // 同じ未変更のカードだと明示的に識別できる場合だけ、画像を
            // 再利用する。 通常の更新は、公開前に全画像を準備する。
            const bool reuse = replace_images && index < replace_images->size() &&
                !(*replace_images)[index] && index < items_.size() &&
                sameValue(items_[index]->data(), source[index]);
            auto item = reuse ? items_[index] :
                std::make_shared<Item>(*this, source[index], next.image_creator, next.title_creator);
            if (!canPublish(next)) return std::nullopt;
            if (prepare_images) item->createImage();
            if (!canPublish(next)) return std::nullopt;
            if (!includeItemExtents(extents, item,
                    [this, &next] { return canPublish(next); })) return std::nullopt;
            const bool selected = selection
                ? std::find(selection->begin(), selection->end(), static_cast<int>(index)) != selection->end()
                : std::any_of(old_values.begin(), old_values.end(), [&](const C& value) {
                    return sameValue(source[index], value);
                });
            if (selected && (multi() || next.selected.empty())) next.selected.insert(static_cast<int>(index));
            next.items.push_back(std::move(item));
        }
        next.width = extents.first;
        next.height = extents.second;
        // Dの更新と同じく、カードの同一性で選択し、カーソルは0から始める。
        next.cursor = next.items.empty() ? -1 : 0;
        if (!multi() && !next.selected.empty()) next.cursor = *next.selected.begin();
        return std::optional<PreparedRefresh>{std::move(next)};
    }

    [[nodiscard]] std::optional<PreparedRefresh> prepareReorder(
        const std::vector<std::size_t>& new_to_old, const std::vector<int>& selection) {
        PreparedRefresh next;
        next.owner = this;
        next.lifetime = timer_lifetime_;
        next.revision = ++refresh_revision_;
        next.selection_revision = selection_revision_;
        if (new_to_old.size() != items_.size()) return std::nullopt;
        std::vector<bool> used(items_.size(), false);
        next.items.reserve(items_.size());
        for (const auto index : new_to_old) {
            if (index >= items_.size() || used[index]) return std::nullopt;
            used[index] = true;
            next.items.push_back(items_[index]);
        }
        for (const auto index : selection) {
            if (index < 0 || index >= count()) continue;
            next.selected.insert(index);
            if (!multi()) break;
        }
        next.image_creator = image_creator_;
        next.title_creator = title_creator_;
        next.width = item_width_;
        next.height = item_height_;
        next.cursor = next.items.empty() ? -1 : 0;
        if (!multi() && !next.selected.empty()) next.cursor = *next.selected.begin();
        return std::optional<PreparedRefresh>{std::move(next)};
    }

    [[nodiscard]] bool canPublish(const PreparedRefresh& next) const noexcept {
        return next.owner == this && next.revision == refresh_revision_ &&
               next.lifetime.lock() == timer_lifetime_ &&
               next.selection_revision == selection_revision_;
    }

    bool publishRefresh(PreparedRefresh& next) {
        if (!canPublish(next)) return false;
        const auto previous_origin = origin_;
        const auto request = next.revision;
        next.owner = nullptr; // 準備した一つの世代は、一度だけ公開できる。
        ++selection_revision_;
        items_.swap(next.items);
        selected_.swap(next.selected);
        image_creator_.swap(next.image_creator);
        title_creator_.swap(next.title_creator);
        item_width_ = next.width;
        item_height_ = next.height;
        cursor_ = next.cursor;
        // 確定時はDと同じく、Shift・マウスの数値添字
        // とドラッグ中フラグを保持し、範囲へ補正する。 D
        // はスクロールで表示座標の開始点を動かし、内容座標
        // の基点を固定する。 この実装は基点を内容座標で
        // 持っているため、更新をまたいでも値を変えない。
        const auto last_index = count() - 1;
        if (shift_anchor_ >= 0) shift_anchor_ = std::min(shift_anchor_, last_index);
        if (mouse_index_ >= 0) mouse_index_ = std::min(mouse_index_, last_index);
        ++range_generation_;
        const auto canceled_range_timer = range_timer_;
        range_timer_ = 0U;
        if (canceled_range_timer != 0U && services_.dispatcher)
            services_.dispatcher->cancel_timer(canceled_range_timer);
        // キャンセル中にディスパッチャーから呼び戻される場合がある。 入
        // れ子の公開は新しい一覧を所有するため、この世代で上書きしない。
        if (refresh_revision_ != request) return false;
        showing_start_index_ = showing_end_index_ = -1;
        layoutItems();
        const Point origin_delta{origin_.x - previous_origin.x,
                                 origin_.y - previous_origin.y};
        if (end_pos_) {
            if (range_pointer_pos_) {
                // Dは終点を現在の表示座標のポインター位置に保つ。 新
                // しい原点を設定してから、内容座標の位置を求め直す。
                end_pos_ = contentPoint(*range_pointer_pos_);
            } else {
                end_pos_->x += origin_delta.x;
                end_pos_->y += origin_delta.y;
            }
        }
        range_refresh_revision_ = request;
        return true;
    }

    void finishRefresh(bool notify = true) {
        const auto request = refresh_revision_;
        const bool refresh_range = range_refresh_revision_ &&
                                   *range_refresh_revision_ == request;
        if (refresh_range) {
            range_refresh_revision_.reset();
            // Dのサイズ変更に伴うスクロール通知は、最後の更新
            // 通知の前に操作中の選択範囲を再計算する。 C++
            // でもその状態を先に適用し、途中の通知を挟まず完
            // 成した選択を一度だけ報告して、一括公開を保つ。
            if (start_pos_ && end_pos_) updateRangeIndices(false);
        }
        if (request != refresh_revision_) return;
        refreshToolTip();
        if (request != refresh_revision_) return;
        // Dと同じ更新後の可視項目数を求める。 途中の一覧を公
        // 開したり、包含ペインより先に画像を送ったりしない。
        if (visible()) repaint(static_cast<GraphicsContext*>(nullptr));
        if (request != refresh_revision_) return;
        redraw();
        if (notify && request == refresh_revision_) callSelectChanged();
        if (request == refresh_revision_ && timer_running_ && range_timer_ == 0U)
            scheduleRangeAutoScroll();
    }

    void refresh(const std::vector<C>& values, ImageCreator create_image,
                 TitleCreator create_title, bool notify = true) {
        auto next = prepareRefresh(values, std::move(create_image), std::move(create_title));
        if (next && publishRefresh(*next)) finishRefresh(notify);
    }

    template <typename Range>
    void refresh(const Range& values, ImageCreator create_image, TitleCreator create_title)
        requires requires { std::begin(values); std::end(values); } {
        std::vector<C> copy(std::begin(values), std::end(values));
        refresh(copy, std::move(create_image), std::move(create_title));
    }

    [[nodiscard]] int count() const noexcept { return static_cast<int>(items_.size()); }
    [[nodiscard]] int selectionCount() const noexcept {
        return static_cast<int>(selected_.size());
    }
    [[nodiscard]] std::vector<ItemPtr> selectionItems() const {
        std::vector<ItemPtr> result;
        result.reserve(selected_.size());
        for (const auto index : selected_)
            if (index >= 0 && index < count()) result.push_back(items_[index]);
        return result;
    }
    [[nodiscard]] std::vector<int> selectionIndices() const {
        return {selected_.begin(), selected_.end()};
    }
    void selectionIndices(const std::vector<int>& indices) {
        std::set<int> next;
        for (const auto index : indices) {
            if (index < 0 || index >= count()) continue;
            next.insert(index);
            if (!multi()) break;
        }
        if (next == selected_) return;
        ++selection_revision_;

        std::set<int> dirty = selected_;
        dirty.insert(next.begin(), next.end());
        selected_ = std::move(next);
        if (selected_.empty()) {
            cursor_ = items_.empty() ? -1 : std::clamp(cursor_, 0, count() - 1);
        } else if (!selected_.contains(cursor_)) {
            cursor_ = *selected_.begin();
        }
        for (const auto index : dirty) redrawCard(index);
    }
    [[nodiscard]] int selection() const noexcept {
        if (selected_.empty()) return -1;
        if (cursor_ >= 0 && selected_.contains(cursor_)) return cursor_;
        return *selected_.begin();
    }

    void refresh(int index, C value) {
        (void)checkedItem(index);
        const auto request = ++refresh_revision_;
        const auto create_image = image_creator_;
        const auto create_title = title_creator_;
        auto next = std::make_shared<Item>(*this, std::move(value), create_image, create_title);
        if (request != refresh_revision_) return;
        next->createImage();
        if (request != refresh_revision_) return;
        auto prepared = items_;
        prepared[static_cast<std::size_t>(index)] = next;
        const auto extents = reflowItemExtents(
            prepared, [this, request] { return request == refresh_revision_; });
        if (!extents) return;
        // 共有のItemPtrをその場で変えず、置き換えを公
        // 開する。 交換前に画像と共通の可変寸法を準備し、旧
        // 項目を保持する読み取り側へ途中の状態を見せない。
        items_.swap(prepared);
        item_width_ = extents->first;
        item_height_ = extents->second;
        layoutItems();
        finishRefresh(false);
    }
    void refresh(int index) { redraw(getBounds(index)); }

    [[nodiscard]] std::vector<C> cards() const {
        std::vector<C> result;
        result.reserve(items_.size());
        for (const auto& item : items_) result.push_back(item->data());
        return result;
    }
    [[nodiscard]] std::vector<C> selectionCards() const {
        std::vector<C> result;
        result.reserve(selected_.size());
        for (const auto index : selected_)
            if (index >= 0 && index < count()) result.push_back(items_[index]->data());
        return result;
    }
    [[nodiscard]] C selectionCard() const {
        const auto index = selection();
        return index < 0 ? C{} : items_[index]->data();
    }
    [[nodiscard]] C card(int index) const { return itemAt(index).data(); }
    [[nodiscard]] bool isSelected() const noexcept { return !selected_.empty(); }
    [[nodiscard]] bool isSelectedAt(int index) const noexcept {
        return selected_.contains(index);
    }

    [[nodiscard]] C search(int x, int y) const {
        const auto index = searchIndex(x, y);
        return index < 0 ? C{} : items_[index]->data();
    }
    [[nodiscard]] int searchIndex(int x, int y) const {
        const auto index = searchIndexLoose(x, y);
        if (index < 0 || index >= count()) return -1;
        const auto& item = *items_[index];
        if (contains(item.imageBounds(), x, y) || contains(item.titleBounds(), x, y)) return index;
        return -1;
    }
    [[nodiscard]] int searchIndexLoose(int x, int y) const {
        // DWTと同じく、表示領域で切り取らず、スクロール後の
        // 内容座標で計算する。 一部だけ見えるカードも対象にし
        // 、候補はsearchIndexで参照前に検証する。
        if (item_width_ <= 0 || item_height_ <= 0 || wrap_ <= 0) return -1;
        const auto column_width = item_width_ + space_x_;
        const auto row_height = item_height_ + space_y_;
        // Dの符号付き整数の除算は、負の余白でもゼロ方向へ切り捨てる。
        const auto column = (static_cast<int>(x + origin_.x) - margin_x_ + space_x_) / column_width;
        const auto row = (static_cast<int>(y + origin_.y) - top_margin_y_ + space_y_) / row_height;
        return row * wrap_ + column;
    }

    void setCardSize(int card_width, int card_height, bool show_title) {
        ++refresh_revision_;
        card_width_ = std::max(0, card_width);
        card_height_ = std::max(0, card_height);
        show_title_ = show_title;
        if (show_title_) card_height_ += title_space_ + font_height_ + 1;
        default_item_width_ = card_width_;
        default_item_height_ = card_height_;
        item_width_ = card_width_;
        item_height_ = card_height_;
        // カード一枚とその名前が、ドックの表示領域に完全に収まるようにす
        // る。 通常のUI文字列ではなく、カード固有の配置として扱う。
        auto layout_item = this->layout_item();
        layout_item.minimum = {static_cast<float>(margin_x_ * 2 + item_width_),
                               static_cast<float>(top_margin_y_ + margin_y_ + item_height_)};
        set_layout_item(layout_item);
        resize();
    }
    void setLayoutValues(int margin_x, int space_x, int margin_y, int space_y,
                         int title_space, int focus_line_padding, int default_wrap) {
        margin_x_ = std::max(0, margin_x);
        space_x_ = std::max(0, space_x);
        margin_y_ = std::max(0, margin_y);
        top_margin_y_ = margin_y_;
        space_y_ = std::max(0, space_y);
        const int image_height = card_height_ - (show_title_ ? title_space_ + font_height_ + 1 : 0);
        title_space_ = std::max(0, title_space);
        focus_line_padding_ = std::max(0, focus_line_padding);
        default_wrap_ = std::max(1, default_wrap);
        setCardSize(card_width_, image_height, show_title_);
    }
    // ALLでは下側の余白を維持しつつ、最初のカー
    // ド行を見出しへ寄せる。 上余白を独立させ、下
    // 側と後続行には従来の半余白の規則を適用する。
    void setTopMargin(int value) {
        top_margin_y_ = std::max(0, value);
        auto layout_item = this->layout_item();
        layout_item.minimum = {static_cast<float>(margin_x_ * 2 + item_width_),
                               static_cast<float>(top_margin_y_ + margin_y_ + item_height_)};
        set_layout_item(layout_item);
        resize();
    }
    [[nodiscard]] int defaultWrap() const noexcept { return default_wrap_; }

    [[nodiscard]] Size computeSize(int width_hint, int height_hint) const {
        return computeSize(width_hint, height_hint, true);
    }
    [[nodiscard]] Size computeSize(int width_hint, int height_hint, bool) const {
        const auto width = width_hint >= 0
                               ? width_hint
                               : margin_x_ * 2 + item_width_ * default_wrap_ +
                                     space_x_ * (default_wrap_ - 1);
        int height = height_hint;
        if (height < 0) {
            const auto rows = items_.empty()
                                  ? 0
                                  : (static_cast<int>(items_.size()) + default_wrap_ - 1) / default_wrap_;
            height = top_margin_y_ + margin_y_ + item_height_ * rows +
                     space_y_ * std::max(0, rows - 1);
        }
        return {static_cast<float>(std::max(0, width)), static_cast<float>(std::max(0, height))};
    }

    [[nodiscard]] std::optional<Size> preferred_size_for_layout(
        const std::optional<float> width_hint,
        const std::optional<float> height_hint) const override {
        const auto to_hint = [](const std::optional<float> value) {
            if (!value || !std::isfinite(*value) || *value < 0.0F) return -1;
            if (*value >= static_cast<float>(std::numeric_limits<int>::max()))
                return std::numeric_limits<int>::max();
            return static_cast<int>(std::lround(*value));
        };
        const bool vertical_scroll = (style_ & Style::VerticalScroll) != 0;
        const int scrollbar_gutter = vertical_scroll ? scrollbarGutterWidth() : 0;
        int client_width_hint = to_hint(width_hint);
        if (client_width_hint >= 0 && vertical_scroll)
            client_width_hint = std::max(0, client_width_hint - scrollbar_gutter);
        auto size = computeSize(client_width_hint, to_hint(height_hint));
        if (vertical_scroll) {
            // DのSWT computeTrimはクライアント寸法に外枠を
            // 加えたサイズを返す。 共通レイアウトからは外側の利用可能幅
            // が渡るため、computeSize前にクライアント幅へ変換
            // する。 外枠より狭い指定でも、最小幅は外枠の分を確保する。
            size.width += static_cast<float>(scrollbar_gutter);
        }
        return size;
    }

    void scroll(int index) {
        if (index < 0 || index >= count()) return;
        calcBounds();
        const bool d_scroll_geometry = !lib::editor_window_host_options().all_mode;
        const int edge_index = d_scroll_geometry ? index : cursor_;
        const auto& item = *items_[index];
        scrollIntoView(item.y(), item.height(), edge_index, d_scroll_geometry,
                       vertical(), true, top_margin_y_, space_y_,
                       [this](int value) { scrollY(value); });
        scrollIntoView(item.x(), item.width(), edge_index, d_scroll_geometry,
                       horizontal(), false, margin_x_, space_x_,
                       [this](int value) { scrollX(value); });
    }

    // DのDragDetectと同じく、Ctrlで指した選択を確定してからドラッグを保存形
    // 式へ変換する。 この一覧の既定の操作元とCardPane独自の操作元の両方から呼ぶ。
    void beginDragSelection() {
        if (dragging_ || mouse_index_ < 0 || mouse_index_ >= count()) return;
        dragging_ = true;
        if (multi() && ctrl_ && !isSelectedAt(mouse_index_)) {
            select(mouse_index_);
            callSelectChanged();
        }
    }

    void setToolTip(ToolTipCreator callback) {
        create_tool_tip_ = std::move(callback);
        refreshToolTip();
    }
    [[nodiscard]] ItemPtr getItem(int index) const {
        if (index < 0 || index >= count()) throw std::out_of_range("card list item index");
        return items_[index];
    }
    [[nodiscard]] int indexOf(const ItemPtr& item) const noexcept {
        for (std::size_t index = 0; index < items_.size(); ++index)
            if (items_[index] == item) return static_cast<int>(index);
        return -1;
    }
    [[nodiscard]] Rectangle getBounds(int index) const {
        const auto& item = *checkedItem(index);
        return {static_cast<float>(item.x()), static_cast<float>(item.y()),
                static_cast<float>(item.width()), static_cast<float>(item.height())};
    }
    [[nodiscard]] Rectangle getImageBounds(int index) const { return checkedItem(index)->imageBounds(); }
    [[nodiscard]] Rectangle getTitleBounds(int index) const { return checkedItem(index)->titleBounds(); }

    [[nodiscard]] std::vector<C> showingCards() const {
        if (showing_start_index_ < 0 || showing_end_index_ <= showing_start_index_) return {};
        std::vector<C> result;
        for (int index = showing_start_index_; index < showing_end_index_; ++index)
            result.push_back(items_[index]->data());
        return result;
    }
    [[nodiscard]] int showingStartIndex() const noexcept { return showing_start_index_; }
    [[nodiscard]] int showingEndIndex() const noexcept { return showing_end_index_; }

    void refreshToolTip() {
        if (!create_tool_tip_) return;
        const auto request = refresh_revision_;
        const auto provider = create_tool_tip_;
        C value{};
        if (mouse_index_ >= 0 && mouse_index_ < count()) value = items_[mouse_index_]->data();
        auto text = provider(value);
        if (request != refresh_revision_) return;
        std::string escaped;
        escaped.reserve(text.size());
        for (const auto ch : text) {
            escaped.push_back(ch);
            if (ch == '&') escaped.push_back('&');
        }
        set_tooltip(std::move(escaped));
    }

    [[nodiscard]] bool isFirstCol(int index) const noexcept { return wrap_ > 0 && index >= 0 && index % wrap_ == 0; }
    [[nodiscard]] bool isFirstRow(int index) const noexcept { return wrap_ > 0 && index >= 0 && index < wrap_; }
    [[nodiscard]] bool isLastRow(int index) const noexcept {
        return wrap_ > 0 && line_ > 0 && index >= (line_ - 1) * wrap_;
    }
    [[nodiscard]] bool isLastCol(int index) const noexcept {
        return wrap_ > 0 && index >= 0 && index % wrap_ == wrap_ - 1;
    }

    void scrollX(int x) {
        const auto content = contentSize();
        const auto viewport = std::max(0, static_cast<int>(clientRectangle().width));
        const auto max_value = std::max(0, static_cast<int>(content.width) - viewport);
        const auto next = static_cast<float>(std::clamp(x, 0, max_value));
        if (origin_.x == next) return;
        origin_.x = next;
        calcBounds();
        updateRangeEndFromPointer();
        updateRangeIndices();
        redraw();
    }
    void scrollY(int y) {
        const auto content = contentSize();
        const auto viewport = std::max(0, static_cast<int>(bounds().height));
        const auto max_value = std::max(0, static_cast<int>(content.height) - viewport);
        const auto next = static_cast<float>(std::clamp(y, 0, max_value));
        if (origin_.y == next) return;
        origin_.y = next;
        calcBounds();
        updateRangeEndFromPointer();
        updateRangeIndices();
        redraw();
    }

    [[nodiscard]] std::optional<VerticalScrollbarGeometry>
    verticalScrollbarGeometry() const noexcept {
        if ((style_ & Style::VerticalScroll) == 0) return std::nullopt;
        const float viewport_height = std::max(0.0F, bounds().height);
        const float content_height = contentSize().height;
        if (viewport_height <= 0.0F || content_height <= viewport_height + 0.5F)
            return std::nullopt;

        const float track_width = std::min(
            std::max(0.0F, bounds().width),
            static_cast<float>(scrollbarGutterWidth()));
        if (track_width <= 0.0F) return std::nullopt;
        const Rectangle track{bounds().width - track_width, 0.0F,
                              track_width, viewport_height};
        const float minimum_thumb = std::min(
            track.height, std::max(24.0F, lib::scaled_ui_metric(28.0F)));
        const float thumb_height = std::clamp(
            track.height * viewport_height / content_height,
            minimum_thumb, track.height);
        const float maximum_scroll = std::max(0.0F, content_height - viewport_height);
        const float travel = std::max(0.0F, track.height - thumb_height);
        const float ratio = maximum_scroll > 0.0F
            ? std::clamp(origin_.y / maximum_scroll, 0.0F, 1.0F) : 0.0F;
        const float thumb_y = track.y + travel * ratio;
        const float inset = std::min(2.0F, track.width * 0.25F);
        return VerticalScrollbarGeometry{
            track,
            {track.x + inset, thumb_y, std::max(1.0F, track.width - inset * 2.0F),
             thumb_height}};
    }

    void calcBounds() {
        if (items_.empty() || wrap_ <= 0) return;
        for (std::size_t index = 0; index < items_.size(); ++index) {
            const auto row = static_cast<int>(index) / wrap_;
            const auto column = static_cast<int>(index) % wrap_;
            items_[index]->x(margin_x_ + column * (item_width_ + space_x_) - static_cast<int>(origin_.x));
            items_[index]->y(top_margin_y_ + row * (item_height_ + space_y_) - static_cast<int>(origin_.y));
        }
    }

    void repaint(GraphicsContext* context, Rectangle viewport = {}) {
        if (!visible()) return;
        ensureLayoutForBounds();
        const auto request = refresh_revision_;
        const auto snapshot = items_;
        calcBounds();
        const auto client = clientRectangle();
        if (viewport.width <= 0.0F || viewport.height <= 0.0F)
            viewport = {0.0F, 0.0F, std::max(0.0F, bounds().width),
                        std::max(0.0F, bounds().height)};
        if (context != nullptr) {
            // ネイティブの再描画は、無効化されたカードの範囲に限
            // 定する。 その領域を消してから描き、未選択カード
            // の下に古い青い名前背景やフォーカス枠を残さない。
            context->fill_rect(
                viewport,
                background().value_or(lib::Color{255U, 255U, 255U, 255U}));
        }
        showing_start_index_ = -1;
        showing_end_index_ = -1;
        for (std::size_t index = 0; index < snapshot.size(); ++index) {
            auto& item = *snapshot[index];
            const auto image = item.imageBounds();
            if (request != refresh_revision_) return;
            const auto title = item.titleBounds();
            if (request != refresh_revision_) return;
            if (!intersects(image, viewport) && !intersects(title, viewport)) continue;
            if (showing_start_index_ < 0) showing_start_index_ = static_cast<int>(index);
            showing_end_index_ = static_cast<int>(index) + 1;

            item.createImage();
            if (request != refresh_revision_) return;
            if (context != nullptr && item.imageResource() != 0U) {
                // コピー元の矩形は画像座標で指定
                // する。画面上の配置先のx/yを
                // そのまま使うと、 2列目以降の
                // カードが切れたり、正しい画像
                // 資源でも空白になったりする。
                const lib::Rect source{0.0F, 0.0F, image.width, image.height};
                context->draw_image(item.imageResource(), source, image,
                                    lib::Interpolation::Nearest);
            }
            if (context != nullptr) {
                const auto callbacks = additionalPaint;
                for (const auto& callback : callbacks) {
                    if (callback) callback(*this, *context, item.data(), image);
                    if (request != refresh_revision_) return;
                }
            }

            if (context != nullptr && !item.title().empty()) {
                const auto text = item.cutText(*context);
                if (request != refresh_revision_) return;
                const auto title_bounds = item.titleBounds();
                if (request != refresh_revision_) return;
                if (selected_.contains(static_cast<int>(index)))
                    context->fill_rect(title_bounds, {40U, 96U, 180U, 255U});
                context->draw_text(text, {title_bounds.x, title_bounds.y}, font_spec_,
                                   selected_.contains(static_cast<int>(index))
                                       ? lib::Color{255U, 255U, 255U, 255U}
                                       : lib::Color{0U, 0U, 0U, 255U});
            }
            if (context != nullptr && cursor_ == static_cast<int>(index) && hasFocus_) {
                const auto focus = inset(image, focus_line_padding_);
                if (focus.width > 0.0F && focus.height > 0.0F)
                    context->draw_rect(focus, {0U, 0U, 0U, 255U}, 1.0F, lib::StrokeStyle::Solid);
                if (!item.title().empty() && title.width > 0.0F && title.height > 0.0F)
                    context->draw_rect(
                        {title.x - 1.0F, title.y - 1.0F,
                         title.width + 2.0F, title.height + 2.0F},
                        {0U, 0U, 0U, 255U}, 1.0F, lib::StrokeStyle::Solid);
            }
            if (context != nullptr && selected_.contains(static_cast<int>(index)))
                context->draw_rect(image, {40U, 96U, 180U, 180U}, 2.0F, lib::StrokeStyle::Solid);
        }
        if (context != nullptr && start_pos_ && end_pos_) {
            auto range = makeRangeRectangle(*start_pos_, *end_pos_);
            range.x -= origin_.x;
            range.y -= origin_.y;
            range = intersection(range, client);
            if (range.width > 0.0F && range.height > 0.0F) {
                context->fill_rect(range, {90U, 150U, 220U, 45U});
                context->draw_rect(range, {50U, 90U, 170U, 180U}, 1.0F,
                                   lib::StrokeStyle::Solid);
            }
        }
        if (context != nullptr) {
            if (const auto scrollbar = verticalScrollbarGeometry();
                scrollbar && intersects(scrollbar->track, viewport)) {
                context->fill_rect(scrollbar->track,
                                   {238U, 240U, 244U, 255U});
                context->draw_rect(scrollbar->track,
                                   {190U, 196U, 205U, 255U}, 1.0F,
                                   lib::StrokeStyle::Solid);
                // つかんでいるつまみは、共通スクロールバーと同じく暗い色にする。
                context->fill_rect(scrollbar->thumb, scrollbar_dragging_
                                   ? lib::Color{64U, 72U, 86U, 255U}
                                   : lib::Color{112U, 124U, 143U, 255U});
            }
        }
    }
    void repaint(GraphicsContext& context, Rectangle viewport = {}) { repaint(&context, viewport); }

    void resize(bool request_redraw = true) {
        const auto request = refresh_revision_;
        layoutItems();
        refreshToolTip();
        if (request != refresh_revision_) return;
        repaint(static_cast<GraphicsContext*>(nullptr));
        if (request_redraw && request == refresh_revision_) redraw();
    }

private:
    // 全体更新と一行更新で、この項目寸法の規則を共用する。 高さ
    // には画像だけでなく、現在の名前用フォントと間隔も含める。
    template <typename IsCurrent>
    [[nodiscard]] bool includeItemExtents(std::pair<int, int>& extents,
                                          const ItemPtr& item,
                                          IsCurrent&& is_current) const {
        if (!is_current()) return false;
        if (default_item_width_ < 0)
            extents.first = std::max(extents.first, item->width());
        if (!is_current()) return false;
        if (default_item_height_ < 0)
            extents.second = std::max(extents.second, item->height());
        return is_current();
    }

    template <typename IsCurrent>
    [[nodiscard]] std::optional<std::pair<int, int>> reflowItemExtents(
        const std::vector<ItemPtr>& items, IsCurrent&& is_current) const {
        std::pair<int, int> extents{
            std::max(0, default_item_width_), std::max(0, default_item_height_)};
        for (const auto& item : items) {
            if (!includeItemExtents(extents, item, is_current)) return std::nullopt;
        }
        return extents;
    }

    void refreshTitleTypography() {
        const int previous_height = font_height_;
        // DWTのCardListと同じく、Editorの
        // 通常の部品フォントを名前にも使う。 固定の10px
        // キャンバス文字ではなく読みやすい寸法を保ち、日本
        // 語名が背景に埋もれないよう中程度の太字にする。
        font_spec_ = lib::effective_editor_widget_font();
        // ALLの小型カード一覧も画像の直下に名
        // 前を置くが、半余白を狭めた分、文字が絵
        // より目立ちすぎないよう少し小さくする。
        // 共通ホストのフォント解決後に、通常
        // の表示サイズから正確に10%縮める。
        if (lib::editor_window_host_options().all_mode)
            font_spec_.points *= 0.9F;
        font_spec_.weight = 600U;
        int measured_height = static_cast<int>(std::ceil(font_spec_.points * 1.25F));
        if (services_.graphics) {
            const auto measured = services_.graphics->measure_text("Mg", font_spec_, 0.0F);
            if (measured.height > 0.0F)
                measured_height = static_cast<int>(std::ceil(measured.height));
        }
        font_height_ = std::max(1, measured_height);
        // DのSWT/MS UI Gothicは文字内の
        // 上余白がほぼなく、名前がカード直下へ来る。 Noto
        // Sans CJKなどは行の箱が高いので、em
        // を超える余分な行高の大部分だけ文字を上へ寄
        // せ、 どのホストでもDの位置へ近づける。
        title_glyph_offset_ = std::max(0, static_cast<int>(std::lround(
            (static_cast<float>(font_height_) - font_spec_.points) * 0.6F)));
        if (show_title_ && previous_height != font_height_) {
            const int difference = font_height_ - previous_height;
            card_height_ = std::max(0, card_height_ + difference);
            default_item_height_ = std::max(0, default_item_height_ + difference);
            item_height_ = std::max(0, item_height_ + difference);
            resize();
        }
    }

    // 配置計算だけを行い、画像提供元・選択通知・再描画を呼ばない。 準
    // 備済み一覧をTableViewとまとめて公開する場合にも使う。
    void layoutItems() {
        const auto width = std::max(0, static_cast<int>(bounds().width));
        layout_width_ = width;
        layout_height_ = std::max(0, static_cast<int>(bounds().height));
        layout_all_mode_ = lib::editor_window_host_options().all_mode;
        const auto layout_grid = [this](const int client_width) {
            if (items_.empty()) {
                wrap_ = 0;
                line_ = 1;
            } else {
                const auto denominator = std::max(1, item_width_ + space_x_);
                wrap_ = std::max(
                    1, (client_width - margin_x_ * 2 + space_x_) / denominator);
                line_ = (count() + wrap_ - 1) / wrap_;
            }
        };
        layout_grid(width);
        if ((style_ & Style::VerticalScroll) != 0 &&
            contentSize().height > static_cast<float>(layout_height_) + 0.5F) {
            layout_grid(std::max(0, width - scrollbarGutterWidth()));
        }
        const auto content = contentSize();
        origin_.x = std::clamp(origin_.x, 0.0F,
                               std::max(0.0F, content.width - bounds().width));
        origin_.y = std::clamp(origin_.y, 0.0F,
                               std::max(0.0F, content.height - bounds().height));
        calcBounds();
    }
public:

    void disposeItems() noexcept {
        ++refresh_revision_;
        ++range_generation_;
        timer_running_ = false;
        stopScrollbarDrag();
        start_pos_.reset();
        end_pos_.reset();
        range_pointer_pos_.reset();
        start_selected_.clear();
        in_ranges_.clear();
        items_.clear();
        selected_.clear();
        cursor_ = -1;
        showing_start_index_ = -1;
        showing_end_index_ = -1;
        if (default_item_width_ >= 0) item_width_ = 0;
        if (default_item_height_ >= 0) item_height_ = 0;
    }

    void updateRangeIndices(bool notify = true) {
        if (!start_pos_ || !end_pos_ || wrap_ <= 0 || item_width_ <= 0 || item_height_ <= 0) return;
        if (!ctrl_) {
            start_selected_.clear();
            for (const auto index : selected_) start_selected_.insert(index);
        }
        auto range = makeRangeRectangle(*start_pos_, *end_pos_);
        // DのCardListはグリッド添字の両端を含め、正規
        // 化した左上が所定の余白の外なら範囲全体を拒否する。
        // 既存の内容座標の基点と、ALLの上余白の拡張は保つ。
        const int left = static_cast<int>(range.x);
        const int top = static_cast<int>(range.y);
        const int right = static_cast<int>(range.x + range.width);
        const int bottom = static_cast<int>(range.y + range.height);
        std::set<int> next;
        if (top >= top_margin_y_ && left >= margin_x_) {
            const int column_step = item_width_ + space_x_;
            const int row_step = item_height_ + space_y_;
            const int column_from = (left - margin_x_ + space_x_) / column_step;
            const int column_to = std::min(wrap_ - 1, (right - margin_x_) / column_step);
            const int row_from = (top - top_margin_y_ + space_y_) / row_step;
            const int row_to = std::min((count() - 1) / wrap_,
                                       (bottom - top_margin_y_) / row_step);
            for (int row = row_from; row <= row_to; ++row)
                for (int column = column_from; column <= column_to; ++column) {
                    const int index = row * wrap_ + column;
                    if (index >= 0 && index < count()) next.insert(index);
                }
        }
        if (next == in_ranges_) return;
        std::set<int> changed = in_ranges_;
        changed.insert(next.begin(), next.end());
        for (const auto index : changed) {
            const auto inside = next.contains(index);
            const auto selected = ctrl_ ? (start_selected_.contains(index) ? !inside : inside) : inside;
            if (selected) select(index); else deselect(index);
        }
        in_ranges_ = std::move(next);
        if (notify) callSelectChanged();
    }

    void ensureLayoutForBounds() {
        const auto width = std::max(0, static_cast<int>(bounds().width));
        const auto height = std::max(0, static_cast<int>(bounds().height));
        const auto all_mode = lib::editor_window_host_options().all_mode;
        if (width == layout_width_ && height == layout_height_ &&
            all_mode == layout_all_mode_) return;

        // refresh()/setCardSize()は、
        // ネイティブ側がCanvasの最終幅を設定する前にも
        // 動く。 最初の幅0の計算でwrap_が1のままにな
        // らないよう、初回描画と分割変更後に寸法を再確認して
        // 配置し直す。 追加の再帰的な再描画要求は送らない。
        resize(false);
    }

    void mouseRelease() {
        if (!multi() || !start_pos_) return;
        ++range_generation_;
        start_pos_.reset();
        end_pos_.reset();
        range_pointer_pos_.reset();
        timer_running_ = false;
        if (services_.dispatcher && range_timer_ != 0U)
            services_.dispatcher->cancel_timer(range_timer_);
        range_timer_ = 0U;
        start_selected_.clear();
        in_ranges_.clear();
        redraw();
    }

    [[nodiscard]] Size contentSize() const noexcept {
        const auto columns = std::max(0, wrap_);
        const auto rows = std::max(0, line_);
        return {static_cast<float>(margin_x_ * 2 + item_width_ * columns +
                                   space_x_ * std::max(0, columns - 1)),
                static_cast<float>(top_margin_y_ + margin_y_ + item_height_ * rows +
                                   space_y_ * std::max(0, rows - 1))};
    }
    [[nodiscard]] int titleSpace() const noexcept { return title_space_; }
    [[nodiscard]] int titleGlyphOffset() const noexcept { return title_glyph_offset_; }
    [[nodiscard]] int fontHeight() const noexcept { return font_height_; }
    [[nodiscard]] const lib::FontSpec& fontSpec() const noexcept { return font_spec_; }
    [[nodiscard]] Size measureText(std::string_view value) const {
        if (services_.graphics) return services_.graphics->measure_text(value, font_spec_, 0.0F);
        return {static_cast<float>(utf8Length(value) * 8U), static_cast<float>(font_height_)};
    }
    [[nodiscard]] const std::vector<ItemPtr>& items() const noexcept { return items_; }

    void setFocus(bool value) {
        hasFocus_ = value;
        if (value) {
            if (on_focus) on_focus();
        } else if (on_blur) {
            on_blur();
        }
    }
    [[nodiscard]] bool hasFocus() const noexcept { return hasFocus_; }

    [[nodiscard]] lib::ClipboardData dragClipboardData() const {
        lib::ClipboardData data;
        for (const auto index : selected_) {
            if (index < 0 || index >= count()) continue;
            if (!data.text.empty()) data.text.push_back('\n');
            data.text += std::to_string(index);
            data.text.push_back(':');
            data.text += items_[index]->title();
        }
        const auto* bytes = reinterpret_cast<const std::byte*>(data.text.data());
        data.formats["application/x-cardwirth-cardlist"] =
            std::vector<std::byte>(bytes, bytes + data.text.size());
        return data;
    }

private:
    struct ListenerEntry {
        std::uint64_t token{};
        SelectionListener callback{};
    };

    [[nodiscard]] static bool sameValue(const C& left, const C& right) {
        if constexpr (requires { left == right; }) return left == right;
        else return std::addressof(left) == std::addressof(right);
    }
    [[nodiscard]] static bool contains(Rectangle rect, int x, int y) noexcept {
        return rect.x <= static_cast<float>(x) && static_cast<float>(x) < rect.x + rect.width &&
               rect.y <= static_cast<float>(y) && static_cast<float>(y) < rect.y + rect.height;
    }
    [[nodiscard]] static bool intersects(Rectangle left, Rectangle right) noexcept {
        return left.width > 0.0F && left.height > 0.0F && right.width > 0.0F && right.height > 0.0F &&
               left.x < right.x + right.width && right.x < left.x + left.width &&
               left.y < right.y + right.height && right.y < left.y + left.height;
    }
    [[nodiscard]] static Rectangle intersection(Rectangle left, Rectangle right) noexcept {
        const float x = std::max(left.x, right.x);
        const float y = std::max(left.y, right.y);
        const float right_edge = std::min(left.x + left.width, right.x + right.width);
        const float bottom_edge = std::min(left.y + left.height, right.y + right.height);
        return {x, y, std::max(0.0F, right_edge - x),
                std::max(0.0F, bottom_edge - y)};
    }
    [[nodiscard]] static Rectangle inset(Rectangle value, int padding) noexcept {
        return {value.x + padding, value.y + padding,
                value.width - padding * 2.0F, value.height - padding * 2.0F};
    }
    [[nodiscard]] static Rectangle makeRangeRectangle(Point first, Point second) noexcept {
        return {std::min(first.x, second.x), std::min(first.y, second.y),
                std::abs(first.x - second.x), std::abs(first.y - second.y)};
    }
    [[nodiscard]] static int floorDiv(int numerator, int denominator) noexcept {
        if (denominator <= 0) return 0;
        const auto quotient = numerator / denominator;
        const auto remainder = numerator % denominator;
        return remainder < 0 ? quotient - 1 : quotient;
    }
    [[nodiscard]] static std::size_t utf8Length(std::string_view text) noexcept {
        std::size_t result = 0U;
        for (std::size_t index = 0; index < text.size(); ++index)
            if ((static_cast<unsigned char>(text[index]) & 0xC0U) != 0x80U) ++result;
        return result;
    }
    [[nodiscard]] Item& itemAt(int index) const { return *checkedItem(index); }
    [[nodiscard]] ItemPtr checkedItem(int index) const {
        if (index < 0 || index >= count()) throw std::out_of_range("card list item index");
        return items_[index];
    }
    [[nodiscard]] Rectangle clientRectangle() const noexcept {
        const auto scrollbar = verticalScrollbarGeometry();
        const float width = scrollbar ? scrollbar->track.x
                                      : std::max(0.0F, bounds().width);
        return {0.0F, 0.0F, width, std::max(0.0F, bounds().height)};
    }
    [[nodiscard]] int scrollbarGutterWidth() const noexcept {
        // DのSWT V_SCROLLは両方のCardPaneに適用される。 このCanvas
        // も共通Editorの幅とCardListの2pxの隙間を、両方で確保する。
        return static_cast<int>(std::ceil(lib::editor_scrollbar_extent() + 2.0F));
    }
    [[nodiscard]] Point contentPoint(Point view_position) const noexcept {
        return {view_position.x + origin_.x, view_position.y + origin_.y};
    }
    void updateRangeEndFromPointer() {
        if (start_pos_ && range_pointer_pos_)
            end_pos_ = contentPoint(*range_pointer_pos_);
    }
    void captureScrollbarPointer(const bool capture) noexcept {
        if (!services_.gui) return;
        try {
            const auto self = widgetHandle();
            if (self) services_.gui->capture_pointer(self, capture);
        } catch (const std::bad_weak_ptr&) {
            // スタック上の一覧には、キャプチャ用の部品識別子がない。
        }
    }
    void stopScrollbarDrag() noexcept {
        if (middle_scroll_pointer_id_) {
            middle_scroll_pointer_id_.reset();
            captureScrollbarPointer(false);
        }
        if (!scrollbar_dragging_) return;
        scrollbar_dragging_ = false;
        scrollbar_pointer_id_.reset();
        captureScrollbarPointer(false);
        redraw(); // 通常のつまみの色へ戻す。
    }
    void updateScrollbarDrag(const float pointer_y) {
        const auto geometry = verticalScrollbarGeometry();
        if (!geometry) {
            stopScrollbarDrag();
            return;
        }
        const float travel = geometry->track.height - geometry->thumb.height;
        const float maximum_scroll = contentSize().height - bounds().height;
        if (travel <= 0.0F || maximum_scroll <= 0.0F) return;
        const float thumb_y = std::clamp(
            pointer_y - scrollbar_pointer_grab_, geometry->track.y,
            geometry->track.y + travel);
        const float ratio = (thumb_y - geometry->track.y) / travel;
        scrollY(static_cast<int>(std::lround(ratio * maximum_scroll)));
    }
    [[nodiscard]] bool horizontal() const noexcept { return (style_ & Style::HorizontalScroll) != 0; }
    [[nodiscard]] bool vertical() const noexcept {
        return (style_ & Style::VerticalScroll) != 0 || !horizontal();
    }
    template <typename Scroll>
    void scrollIntoView(int position, int extent, int edge_index,
                        bool d_scroll_geometry, bool enabled, bool vertical_axis,
                        int margin, int space,
                        Scroll scroll) {
        if (!enabled) return;
        const auto viewport = vertical_axis
            ? static_cast<int>(bounds().height)
            : static_cast<int>(clientRectangle().width);
        const auto current = vertical_axis ? static_cast<int>(origin_.y) : static_cast<int>(origin_.x);
        const bool first = vertical_axis ? isFirstRow(edge_index) : isFirstCol(edge_index);
        const bool last = vertical_axis ? isLastRow(edge_index) : isLastCol(edge_index);
        const auto before = position + current - (first ? margin : space);
        const auto after = position + current + extent + (last ? margin : space);
        // Dのスクロールバーは、端の余白を含む補正済みの項目範囲をつま
        // みと比較する。 ALLは既存の表示範囲基準の規則を維持する。
        if (before <= current && after >= current + viewport) return;
        if (before < current) {
            scroll(before);
        } else if (after > current + viewport) {
            const bool adjusted_item_exceeds_viewport = d_scroll_geometry
                ? after - before > viewport
                : extent > viewport;
            scroll(adjusted_item_exceeds_viewport ? before : after - viewport);
        }
    }
    void bindDragSource() {
        if (!default_drag_source_enabled_ || !services_.drag_drop) return;
        const auto handle = widgetHandle();
        services_.drag_drop->bind_source({
            handle, lib::DropOperation::Copy | lib::DropOperation::Move,
            [this] { return dragClipboardData(); },
            [](lib::DropOperation) {}});
    }
    void scheduleRangeAutoScroll() {
        if (!timer_running_ || !services_.dispatcher) return;
        const auto generation = range_generation_;
        const std::weak_ptr<int> lifetime = timer_lifetime_;
        range_timer_ = services_.dispatcher->post_delayed(
            std::chrono::milliseconds(16), [this, generation, lifetime] {
                if (lifetime.expired() || generation != range_generation_ || !timer_running_) return;
                const auto current = [&] {
                    return !lifetime.expired() && generation == range_generation_ && timer_running_;
                };
                ++frame_;
                const auto viewport = clientRectangle();
                if (range_pointer_pos_) {
                    if (range_pointer_pos_->x < 0.0F)
                        scrollX(static_cast<int>(origin_.x) - 16);
                    else if (range_pointer_pos_->x >= viewport.width)
                        scrollX(static_cast<int>(origin_.x) + 16);
                    if (!current()) return;
                    if (range_pointer_pos_->y < 0.0F)
                        scrollY(static_cast<int>(origin_.y) - 16);
                    else if (range_pointer_pos_->y >= viewport.height)
                        scrollY(static_cast<int>(origin_.y) + 16);
                    if (!current()) return;
                    updateRangeIndices();
                }
                if (current()) scheduleRangeAutoScroll();
            });
    }
    void handlePointer(lib::PointerEvent& event) {
        ensureLayoutForBounds();
        modifiers_ = event.modifiers;
        shift_ = hasModifier(event.modifiers, lib::Modifiers::Shift);
        ctrl_ = hasModifier(event.modifiers, lib::Modifiers::Control);
        const auto x = static_cast<int>(std::lround(event.position.x));
        const auto y = static_cast<int>(std::lround(event.position.y));
        // 仕様変更(D版と異なる): 中ボタンのドラッグでカード一覧をスクロールする
        // (ALL・単体エディタ共通)。内容がポインタに付いて動く(掴んで動かす)。
        if (middle_scroll_pointer_id_) {
            if (event.pointer_id == *middle_scroll_pointer_id_) {
                if (event.action == lib::PointerAction::Move) {
                    const auto dx = event.position.x - middle_scroll_start_.x;
                    const auto dy = event.position.y - middle_scroll_start_.y;
                    if (horizontal())
                        scrollX(static_cast<int>(std::lround(middle_scroll_origin_.x - dx)));
                    if (vertical())
                        scrollY(static_cast<int>(std::lround(middle_scroll_origin_.y - dy)));
                } else if (event.action == lib::PointerAction::Up ||
                           event.action == lib::PointerAction::Cancel) {
                    middle_scroll_pointer_id_.reset();
                    captureScrollbarPointer(false);
                }
            }
            event.handled = true;
            return;
        }
        if (event.action == lib::PointerAction::Down &&
            event.button == lib::PointerButton::Middle && !scrollbar_dragging_) {
            middle_scroll_pointer_id_ = event.pointer_id;
            middle_scroll_start_ = event.position;
            middle_scroll_origin_ = origin_;
            captureScrollbarPointer(true);
            event.handled = true;
            return;
        }
        if (scrollbar_dragging_) {
            const bool owns_drag = scrollbar_pointer_id_ &&
                                   event.pointer_id == *scrollbar_pointer_id_;
            if (owns_drag && event.action == lib::PointerAction::Move) {
                updateScrollbarDrag(event.position.y);
            } else if (owns_drag &&
                       (event.action == lib::PointerAction::Up ||
                        event.action == lib::PointerAction::Cancel)) {
                stopScrollbarDrag();
            }
            event.handled = true;
            return;
        }
        if (event.action == lib::PointerAction::Cancel) {
            mouseRelease();
            event.handled = true;
            return;
        }
        if (event.action == lib::PointerAction::Move) {
            const auto index = searchIndex(x, y);
            if (mouse_index_ != index) {
                mouse_index_ = index;
                refreshToolTip();
            }
            if (multi() && start_pos_) {
                range_pointer_pos_ = event.position;
                end_pos_ = contentPoint(event.position);
                updateRangeIndices();
            }
            return;
        }
        if (event.action == lib::PointerAction::Down) {
            if (event.button == lib::PointerButton::Primary || event.button == lib::PointerButton::Secondary) {
                setFocus(true);
                if (services_.gui) services_.gui->focus(widgetHandle());
            }
            dragging_ = false;
            const auto scrollbar = verticalScrollbarGeometry();
            if (scrollbar && contains(scrollbar->track, x, y)) {
                if (event.button == lib::PointerButton::Primary) {
                    if (contains(scrollbar->thumb, x, y)) {
                        scrollbar_dragging_ = true;
                        scrollbar_pointer_id_ = event.pointer_id;
                        scrollbar_pointer_grab_ =
                            event.position.y - scrollbar->thumb.y;
                        captureScrollbarPointer(true);
                        redraw(); // 押下中のつまみの色。
                    } else if (event.position.y < scrollbar->thumb.y) {
                        scrollY(static_cast<int>(origin_.y) -
                                static_cast<int>(bounds().height));
                    } else {
                        scrollY(static_cast<int>(origin_.y) +
                                static_cast<int>(bounds().height));
                    }
                }
                event.handled = true;
                return;
            }
            const auto index = searchIndex(x, y);
            mouse_index_ = index;
            if (index >= 0) {
                if (!multi()) {
                    setCursor(index);
                } else if (event.button == lib::PointerButton::Primary) {
                    if (shift_) {
                        if (shift_anchor_ >= 0) {
                            deselectAll();
                            const auto first = std::min(shift_anchor_, index);
                            const auto last = std::max(shift_anchor_, index);
                            for (int value = first; value <= last; ++value) select(value);
                        } else {
                            select(index);
                            shift_anchor_ = index;
                        }
                        callSelectChanged();
                    } else if (!ctrl_) {
                        if (!isSelectedAt(index)) deselectAll();
                        select(index);
                        shift_anchor_ = index;
                        callSelectChanged();
                    } else {
                        shift_anchor_ = index;
                    }
                    setCursor(index, true, false);
                } else if (event.button == lib::PointerButton::Secondary && !ctrl_) {
                    if (!isSelectedAt(index)) deselectAll();
                    shift_anchor_ = index;
                    select(index);
                    setCursor(index);
                    callSelectChanged();
                }
            } else if (event.button == lib::PointerButton::Primary) {
                shift_anchor_ = -1;
                mouse_index_ = -1;
                if (multi()) startRangeSelection(event.position);
                else deselectAll();
                callSelectChanged();
            } else if (event.button == lib::PointerButton::Secondary) {
                // DはCtrl/Shift付きでも、空白で副ボタンを押すと
                // 選択解除を通知する。 主ボタンの範囲選択は開始しない。
                shift_anchor_ = -1;
                mouse_index_ = -1;
                deselectAll();
                callSelectChanged();
            }
            // CardListは選択をすぐ適用し、ダブルクリックの認識
            // とドラッグ準備は共通MouseInputに任せる。 副
            // ボタンの押下は、コンテキスト操作として完全に消費する。
            event.handled = event.button != lib::PointerButton::Primary;
            return;
        }
        if (event.action == lib::PointerAction::Up) {
            if (event.button == lib::PointerButton::Primary && start_pos_) {
                mouseRelease();
                event.handled = true;
                return;
            }
            if (!dragging_ && multi() && event.button == lib::PointerButton::Primary && mouse_index_ >= 0) {
                if (ctrl_) {
                    if (isSelectedAt(mouse_index_)) deselect(mouse_index_); else select(mouse_index_);
                    callSelectChanged();
                } else if (!shift_) {
                    deselectAll();
                    select(mouse_index_);
                    callSelectChanged();
                }
            }
            dragging_ = false;
            mouse_index_ = -1;
            event.handled = true;
        }
    }
    void handleKeyUp(lib::KeyEvent& event) {
        modifiers_ = event.modifiers;
        shift_ = hasModifier(event.modifiers, lib::Modifiers::Shift);
        ctrl_ = hasModifier(event.modifiers, lib::Modifiers::Control);
    }
    void handleKeyDown(lib::KeyEvent& event) {
        modifiers_ = event.modifiers;
        shift_ = hasModifier(event.modifiers, lib::Modifiers::Shift);
        ctrl_ = hasModifier(event.modifiers, lib::Modifiers::Control);
        if (items_.empty()) return;
        // Dと同じくCtrl+Home/End/Pageのスクロールは許す。下で無視するのはCtrl+矢印だけ。
        const auto update_cursor = [this](int value) {
            if (!ctrl_ && !shift_) deselectAll();
            setCursor(value, true, !multi() || !shift_);
            if (shift_ && multi()) {
                const auto first = std::min(shift_anchor_, cursor_);
                const auto last = std::max(shift_anchor_, cursor_);
                deselectAll();
                for (int index = std::max(0, first); index <= last; ++index) select(index);
                callSelectChanged();
            } else {
                shift_anchor_ = cursor_;
            }
        };
        switch (event.key) {
        case lib::Key::PageUp: scrollY(static_cast<int>(origin_.y) - static_cast<int>(bounds().height)); break;
        case lib::Key::PageDown: scrollY(static_cast<int>(origin_.y) + static_cast<int>(bounds().height)); break;
        case lib::Key::Home: scrollY(0); break;
        case lib::Key::End: scrollY(std::numeric_limits<int>::max()); break;
        case lib::Key::Up: {
            if (ctrl_) return;
            int next = cursor_ - wrap_;
            if (next < 0) {
                next = std::max(0, (line_ - 1) * wrap_ + cursor_);
                if (next >= count()) next = std::max(0, next - wrap_);
            }
            update_cursor(next);
            break;
        }
        case lib::Key::Down: {
            if (ctrl_) return;
            int next = cursor_ + wrap_;
            if (next >= count()) next = cursor_ % std::max(1, wrap_);
            update_cursor(next);
            break;
        }
        case lib::Key::Left: {
            if (ctrl_) return;
            const auto next = isFirstCol(cursor_) ?
                                  std::min(count() - 1, cursor_ + wrap_ - 1) : cursor_ - 1;
            update_cursor(std::max(0, next));
            break;
        }
        case lib::Key::Right: {
            if (ctrl_) return;
            int next = cursor_;
            if (cursor_ == count() - 1) {
                const auto remainder = count() % std::max(1, wrap_);
                next = count() - (remainder == 0 ? wrap_ : remainder);
            } else if (isLastCol(cursor_)) next = cursor_ - wrap_ + 1;
            else ++next;
            update_cursor(std::clamp(next, 0, count() - 1));
            break;
        }
        default: return;
        }
        event.handled = true;
    }

    // コンテナーがCardListを所有するため、逆リンクでコンテナーを保持しない。
    std::weak_ptr<lib::Container> parent_{};
    int style_{};
    Services services_{};
    ImageCreator image_creator_{};
    TitleCreator title_creator_{};
    ToolTipCreator create_tool_tip_{};
    std::vector<ItemPtr> items_{};
    std::set<int> selected_{};
    std::vector<ListenerEntry> listeners_{};
    std::uint64_t next_listener_token_{1U};
    std::uint64_t redraw_revision_{};
    std::uint64_t paint_revision_{};
    std::uint64_t refresh_revision_{};
    std::optional<std::uint64_t> range_refresh_revision_{};
    std::uint64_t selection_revision_{};
    std::uint64_t range_generation_{};
    std::shared_ptr<int> timer_lifetime_{std::make_shared<int>(0)};
    Point origin_{};
    std::optional<Point> range_pointer_pos_{};
    int cursor_{-1};
    int card_width_{};
    int card_height_{};
    bool show_title_{};
    int item_width_{};
    int item_height_{};
    int default_item_width_{-1};
    int default_item_height_{-1};
    int wrap_{};
    int line_{};
    int layout_width_{-1};
    int layout_height_{-1};
    bool layout_all_mode_{};
    int margin_x_{25};
    int space_x_{25};
    int margin_y_{20};
    int top_margin_y_{20};
    int space_y_{20};
    int title_space_{5};
    // フォントの行の箱の中で、文字の上にある空白（refreshFont
    // 参照）。 名前はこの分だけ上へ描く。
    int title_glyph_offset_{};
    int default_wrap_{4};
    int font_height_{15};
    int focus_line_padding_{2};
    int shift_anchor_{-1};
    int mouse_index_{-1};
    bool dragging_{};
    bool scrollbar_dragging_{};
    std::optional<std::uint64_t> scrollbar_pointer_id_{};
    // 中ボタンでつかんでスクロールする操作。
    std::optional<std::uint64_t> middle_scroll_pointer_id_{};
    lib::Point middle_scroll_start_{};
    lib::Point middle_scroll_origin_{};
    float scrollbar_pointer_grab_{};
    bool shift_{};
    bool ctrl_{};
    bool hasFocus_{};
    int showing_start_index_{-1};
    int showing_end_index_{-1};
    std::uint64_t frame_{};
    std::uint64_t last_vertical_frame_{};
    std::uint64_t last_horizontal_frame_{};
    bool timer_running_{};
    lib::TimerId range_timer_{};
    std::set<int> start_selected_{};
    std::set<int> in_ranges_{};
    std::optional<Point> start_pos_{};
    std::optional<Point> end_pos_{};
    lib::Modifiers modifiers_{lib::Modifiers::None};
    lib::FontSpec font_spec_{"", 12.0F, 600U, false, false, false};
    bool default_drag_source_enabled_{true};

    template <typename>
    friend class CardListDragSourceEffect;
};

template <typename C>
void CardListDragSourceEffect<C>::dragStart(lib::DragEvent& event) {
    if (list_ == nullptr) return;
    const auto index = list_->searchIndex(static_cast<int>(std::lround(event.position.x)),
                                          static_cast<int>(std::lround(event.position.y)));
    if (index < 0) return;
    list_->beginDragSelection();
    event.data = list_->dragClipboardData();
    event.allowed = lib::DropOperation::Copy | lib::DropOperation::Move;
    event.selected = lib::DropOperation::Copy;
    event.handled = true;
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::cardlist
