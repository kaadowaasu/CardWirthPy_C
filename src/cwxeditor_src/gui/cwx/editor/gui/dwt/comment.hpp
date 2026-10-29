#pragma once

// cwx/editor/gui/dwt/comment.d
// の移植可能なC++20実装。 三種類の部品
// へコメント・警告の印を描く元の状態を、共通
// の部品・イベント・描画・文字・クリップボー
// ドで表す。 ホストはネイティブハンドルを公
// 開せず、任意のGUIへ通知を接続できる。

#include "cwxeditor_src/core/cwx/event.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/editdialogsession.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/xmlbytestransfer.hpp"
#include "cwxeditor_src/lib/clipboard.hpp"
#include "cwxeditor_src/lib/components.hpp"
#include "cwxeditor_src/lib/events.hpp"
#include "cwxeditor_src/lib/graphics.hpp"
#include "cwxeditor_src/lib/services.hpp"
#include "cwxeditor_src/lib/text.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::dprops { class Props; }

namespace cwxeditor::cwx::editor::gui::dwt::dmenu {
class TextMenuModify;
}

namespace cwxeditor::cwx::editor::gui::dwt::comment {

namespace lib = ::cwxeditor::lib;
using String = lib::String;
using StringView = lib::StringView;
using Services = lib::Services;
using Point = lib::Point;
using Rect = lib::Rect;
using Size = lib::Size;
using ResourceId = lib::ResourceId;
using PathObject = ::cwxeditor::cwx::path::CWXPath;
using WarningList = lib::Array<String>;

inline constexpr std::size_t npos = (std::numeric_limits<std::size_t>::max)();

struct Image {
    ResourceId resource{};
    Size logical_size{16.0F, 16.0F};
    lib::ImageData data{};

    [[nodiscard]] bool valid() const noexcept {
        return resource != 0 || !data.frames.empty();
    }
    [[nodiscard]] Rect bounds() const noexcept {
        return {0.0F, 0.0F, logical_size.width, logical_size.height};
    }
};

struct CommentImages {
    Image comment{};
    Image warning{};

    [[nodiscard]] Image menu(
        ::cwxeditor::cwx::types::MenuID menu_id) const noexcept {
        return menu_id == ::cwxeditor::cwx::types::MenuID::Comment
                   ? comment
                   : Image{};
    }
    [[nodiscard]] Image menu(std::int32_t menu_id) const noexcept {
        return menu_id == static_cast<std::int32_t>(
                              ::cwxeditor::cwx::types::MenuID::Comment)
                   ? comment
                   : Image{};
    }
};

struct CommentEtc {
    std::size_t textTabs{4};
    bool xmlCopy{};
    std::int32_t warningImageWidth{200};
    std::int32_t warningImageWidthForTable{80};
    std::int32_t warningImageWidthForTree{80};
    std::int32_t warningImageWidthForCardList{50};
    lib::Color warningImageColor{255, 128, 128, 255};
    std::size_t rowHeight{20};
};

struct CommentVar { CommentEtc etc{}; };

struct CommentLooks {
    lib::FontSpec textDlgFont(float default_size) const {
        lib::FontSpec result;
        result.points = default_size > 0.0F ? default_size : 10.0F;
        return result;
    }
};

struct CommentMessages {
    String dlgTitComment{"コメントの記述"};
    String dlgTextOK{"&OK"};
    String dlgTextCancel{"キャンセル(&C)"};
    String dlgTextApply{"適用(&A)"};
    String dlgTextClose{"閉じる(&C)"};
};

struct Props {
    Services services{};
    CommentImages images{};
    CommentVar var{};
    CommentLooks looks{};
    CommentMessages msgs{};
    std::function<std::function<void()>(const std::shared_ptr<lib::Window>&)> bindDialogSize{};
};

// すべてのEditorホストで、所有権付きの同じ資源一覧からコメント画像を取得する。
void bindEditorImages(Props& target, dprops::Props& editor);

class Signal final {
public:
    using Token = std::size_t;

    [[nodiscard]] Token add(lib::Action action) {
        if (!action) return 0;
        const Token token = next_token_++;
        listeners_.push_back({token, std::move(action)});
        return token;
    }
    void remove(Token token) {
        if (token == 0) return;
        listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(),
                                        [token](const Entry& entry) {
                                            return entry.token == token;
                                        }),
                         listeners_.end());
    }
    void notify() const {
        const auto snapshot = listeners_;
        for (const auto& entry : snapshot)
            if (entry.action) entry.action();
    }
    [[nodiscard]] std::size_t size() const noexcept { return listeners_.size(); }

private:
    struct Entry { Token token{}; lib::Action action{}; };
    Token next_token_{1};
    std::vector<Entry> listeners_{};
};

struct Commons {
    Props prop{};
    Services services{};
    Signal replText{};

    Commons() = default;
    explicit Commons(Props value)
        : prop(std::move(value)), services(prop.services) {}
    Commons(Props value, Services service)
        : prop(std::move(value)), services(std::move(service)) {
        prop.services = services;
    }

    [[nodiscard]] const Services& effectiveServices() const noexcept {
        return services.gui || services.graphics || services.clipboard ||
                       services.text || services.dispatcher
                   ? services
                   : prop.services;
    }
};

enum class DrawingType : std::uint8_t { None, Comment, Warning };

template <typename Item>
using DrawingItemPointer = std::conditional_t<
    std::is_pointer_v<std::remove_cv_t<Item>>, Item,
    std::add_pointer_t<std::remove_reference_t<Item>>>;

template <typename Item>
class DrawingCW final {
public:
    DrawingItemPointer<Item> item{};
    DrawingType type{DrawingType::None};

    void clear() noexcept { item = {}; type = DrawingType::None; }
};

class ToolTip final {
public:
    String message{};
    Rect icon_rect{};
    Point location{};
    bool visible{};
    bool auto_hide{false};
    std::shared_ptr<lib::ToolTip> widget{};
    lib::Action mouse_track{};
};

class Table;
class Tree;

struct TableItem {
    Table* owner{};
    std::size_t index{};
    PathObject* value{};
    Rect item_bounds{};
    bool disposed{};

    [[nodiscard]] Rect bounds() const noexcept { return item_bounds; }
    [[nodiscard]] bool isDisposed() const noexcept;
    [[nodiscard]] PathObject* data() const noexcept { return value; }
};

struct TreeItem {
    Tree* owner{};
    lib::TreeItemId id{};
    PathObject* value{};
    Rect item_bounds{};
    bool disposed{};

    [[nodiscard]] Rect bounds() const noexcept { return item_bounds; }
    [[nodiscard]] bool isDisposed() const noexcept;
    [[nodiscard]] PathObject* data() const noexcept { return value; }
};

class Table : public lib::TableView {
public:
    using PaintCallback = std::function<void(lib::DrawingContext&)>;

    std::vector<PathObject*> item_paths{};
    std::vector<Rect> row_bounds{};
    std::vector<TableItem> comment_items{};
    float row_height{20.0F};
    std::size_t top_index{};
    std::size_t visible_rows{};
    bool disposed{};
    std::uint64_t redraw_revision{};
    Signal::Token comment_repl_token{};
    Commons* comment_comm{};
    PaintCallback on_paint{};
    lib::Action on_redraw{};
    std::shared_ptr<ToolTip> comment_tooltip{};
    DrawingCW<TableItem> comment_drawing{};

    [[nodiscard]] TableItem item(std::size_t index) noexcept;
    [[nodiscard]] TableItem& commentItem(std::size_t index) noexcept;
    [[nodiscard]] Rect clientBounds() const noexcept { return bounds(); }
    void redraw() noexcept;
    void redraw(Rect dirty) noexcept;
    void paint(lib::DrawingContext& context) { if (on_paint) on_paint(context); }
};

class Tree : public lib::TreeView {
public:
    using PaintCallback = std::function<void(lib::DrawingContext&)>;

    std::vector<PathObject*> item_paths{};
    std::vector<TreeItem> comment_items{};
    float row_height{20.0F};
    std::size_t top_index{};
    std::size_t visible_rows{};
    bool disposed{};
    std::uint64_t redraw_revision{};
    Signal::Token comment_repl_token{};
    Commons* comment_comm{};
    PaintCallback on_paint{};
    lib::Action on_redraw{};
    std::shared_ptr<ToolTip> comment_tooltip{};
    DrawingCW<TreeItem> comment_drawing{};

    [[nodiscard]] TreeItem item(lib::TreeItemId id) noexcept;
    [[nodiscard]] TreeItem& commentItem(lib::TreeItemId id) noexcept;
    [[nodiscard]] std::vector<TreeItem> visibleItems() noexcept;
    void redraw() noexcept;
    void redraw(Rect dirty) noexcept;
    void paint(lib::DrawingContext& context) { if (on_paint) on_paint(context); }
};

template <typename C>
class CardList : public lib::Widget {
public:
    using PaintCallback = std::function<void(lib::DrawingContext&)>;

    [[nodiscard]] lib::ComponentKind kind() const noexcept override {
        return lib::ComponentKind::Canvas;
    }

    std::vector<C> cards{};
    std::vector<Rect> image_bounds{};
    std::size_t showingStartIndex{};
    std::size_t showingEndIndex{npos};
    bool disposed{};
    std::uint64_t redraw_revision{};
    Signal::Token comment_repl_token{};
    Commons* comment_comm{};
    PaintCallback on_paint{};
    lib::Action on_redraw{};
    std::shared_ptr<ToolTip> comment_tooltip{};
    DrawingCW<C> comment_drawing{};

    [[nodiscard]] std::size_t endIndex() const noexcept {
        return std::min(showingEndIndex, cards.size());
    }
    [[nodiscard]] Rect getImageBounds(std::size_t index) const noexcept {
        if (index < image_bounds.size()) return image_bounds[index];
        const auto b = bounds();
        return {b.x, b.y + static_cast<float>(index) * 20.0F, b.width, 20.0F};
    }
    [[nodiscard]] C* card(std::size_t index) noexcept {
        return index < cards.size() ? &cards[index] : nullptr;
    }
    [[nodiscard]] const C* card(std::size_t index) const noexcept {
        return index < cards.size() ? &cards[index] : nullptr;
    }
    [[nodiscard]] C* search(Point point) noexcept {
        const auto end = endIndex();
        const auto begin = std::min(showingStartIndex, end);
        for (std::size_t index = begin; index < end; ++index) {
            const auto b = getImageBounds(index);
            if (b.x <= point.x && point.x < b.x + b.width &&
                b.y <= point.y && point.y < b.y + b.height)
                return card(index);
        }
        return nullptr;
    }
    [[nodiscard]] std::size_t indexOf(const C* value) const noexcept {
        if (!value || cards.empty()) return npos;
        const auto first = cards.data();
        if (value < first || value >= first + cards.size()) return npos;
        return static_cast<std::size_t>(value - first);
    }
    void redraw() noexcept;
    void redraw(Rect dirty) noexcept;
    void paint(lib::DrawingContext& context) { if (on_paint) on_paint(context); }
};

template <typename C>
void CardList<C>::redraw() noexcept {
    ++redraw_revision;
    if (on_redraw) on_redraw();
}

template <typename C>
void CardList<C>::redraw(Rect) noexcept {
    redraw();
}

[[nodiscard]] String commentText(const PathObject* object, bool is_event_view);
[[nodiscard]] Point commentPos(const Rect& comment_bounds, const TableItem& item);
[[nodiscard]] Point commentPos(const Rect& comment_bounds, const TreeItem& item);
[[nodiscard]] Point commentPos(const Rect& comment_bounds, const Rect& item_bounds);
[[nodiscard]] Point warningPos(const Rect& comment_bounds, const Rect& warning_bounds,
                               const TableItem& item, bool is_event_view);
[[nodiscard]] Point warningPos(const Rect& comment_bounds, const Rect& warning_bounds,
                               const TreeItem& item, bool is_event_view);
[[nodiscard]] Point warningPos(const Rect& comment_bounds, const Rect& warning_bounds,
                               const Rect& item_bounds, bool has_comment);

[[nodiscard]] PathObject* commentable(TableItem& item) noexcept;
[[nodiscard]] const PathObject* commentable(const TableItem& item) noexcept;
[[nodiscard]] PathObject* commentable(TreeItem& item) noexcept;
[[nodiscard]] const PathObject* commentable(const TreeItem& item) noexcept;

template <typename T>
[[nodiscard]] PathObject* commentable(T& value) noexcept {
    using U = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_pointer_v<U>) {
        using P = std::remove_pointer_t<U>;
        if constexpr (std::is_base_of_v<PathObject, std::remove_cv_t<P>>)
            return value;
        return nullptr;
    } else if constexpr (requires { value.get(); }) {
        using P = std::remove_pointer_t<decltype(value.get())>;
        if constexpr (std::is_base_of_v<PathObject, std::remove_cv_t<P>>)
            return value.get();
        return nullptr;
    } else if constexpr (std::is_base_of_v<PathObject, U>) {
        return &value;
    } else {
        return nullptr;
    }
}

template <typename T>
[[nodiscard]] const PathObject* commentable(const T& value) noexcept {
    return commentable(const_cast<T&>(value));
}

[[nodiscard]] Image tiltWarningImage(const Props& prop,
                                     std::int32_t warning_image_width = -1);
[[nodiscard]] Image tiltWarningImage(const Props& prop,
                                     lib::GraphicsBackend& graphics,
                                     std::int32_t warning_image_width = -1);

template <typename Item>
void drawCommentAndWarning(const Props& prop, bool is_event_view,
                           const Image& comment_image, const Rect& comment_bounds,
                           const Image& warning_icon, const Rect& warning_bounds,
                           float client_x, float client_width, const Item& item,
                           lib::DrawingContext& context, DrawingCW<Item>& drawing,
                           const WarningList& warnings, std::int32_t warning_width,
                           Image& warning_image) {
    (void)drawing;
    Rect item_bounds{};
    if constexpr (std::is_same_v<std::remove_cv_t<Item>, TableItem> ||
                  std::is_same_v<std::remove_cv_t<Item>, TreeItem>) {
        item_bounds = item.bounds();
    } else if constexpr (requires { item.bounds(); }) {
        item_bounds = item.bounds();
    }
    const auto* object = commentable(item);
    const auto text = commentText(object, is_event_view);
    if (!warnings.empty()) {
        if (!warning_image.valid()) {
            if (prop.services.graphics)
                warning_image = tiltWarningImage(prop, *prop.services.graphics,
                                                 warning_width);
            else
                warning_image = tiltWarningImage(prop, warning_width);
        }
        context.draw_image(warning_image.resource,
                           {0.0F, 0.0F, warning_image.logical_size.width, 1.0F},
                           {client_x + client_width - warning_image.logical_size.width,
                            item_bounds.y, warning_image.logical_size.width,
                            item_bounds.height}, lib::Interpolation::Nearest);
        Point position{};
        if constexpr (std::is_same_v<std::remove_cv_t<Item>, TableItem>)
            position = warningPos(comment_bounds, warning_bounds, item, is_event_view);
        else if constexpr (std::is_same_v<std::remove_cv_t<Item>, TreeItem>)
            position = warningPos(comment_bounds, warning_bounds, item, is_event_view);
        else
            position = warningPos(comment_bounds, warning_bounds, item_bounds, !text.empty());
        context.draw_image(warning_icon.resource, warning_icon.bounds(),
                           {position.x, position.y, warning_bounds.width,
                            warning_bounds.height}, lib::Interpolation::Nearest);
    }
    if (!text.empty()) {
        Point position{};
        if constexpr (std::is_same_v<std::remove_cv_t<Item>, TableItem>)
            position = commentPos(comment_bounds, item);
        else if constexpr (std::is_same_v<std::remove_cv_t<Item>, TreeItem>)
            position = commentPos(comment_bounds, item);
        else
            position = commentPos(comment_bounds, item_bounds);
        context.draw_image(comment_image.resource, comment_image.bounds(),
                           {position.x, position.y, comment_bounds.width,
                            comment_bounds.height}, lib::Interpolation::Nearest);
    }
}

void createOrDestroyToolTip(lib::Widget& parent, StringView message,
                            std::shared_ptr<ToolTip>& tool_tip,
                            std::optional<Rect> icon_rect = std::nullopt,
                            lib::Action mouse_track = {});
void createOrDestroyToolTip(lib::Widget& parent, StringView message,
                            std::unique_ptr<ToolTip>& tool_tip,
                            std::optional<Rect> icon_rect = std::nullopt,
                            lib::Action mouse_track = {});

template <typename Surface, typename Item>
void setupCommentToolTip(Commons& comm, Surface& surface, bool is_event_view,
                         std::function<WarningList(const Item&)> get_warnings,
                         DrawingCW<Item>& drawing,
                         std::function<Item*(Point)> hit_test,
                         std::function<Point(const Item&)> comment_position,
                         std::function<Point(const Item&)> warning_position) {
    const Image comment_image = comm.prop.images.comment;
    const Image warning_image = comm.prop.images.warning;
    const auto comment_bounds = comment_image.bounds();
    const auto warning_bounds = warning_image.bounds();
    auto previous = surface.on_pointer;
    surface.on_pointer = [&, previous, comment_bounds, warning_bounds,
                          get_warnings = std::move(get_warnings),
                          hit_test = std::move(hit_test),
                          comment_position = std::move(comment_position),
                          warning_position = std::move(warning_position)]
                         (lib::PointerEvent& event) mutable {
        if (previous) previous(event);
        Item* item = nullptr;
        if (event.action != lib::PointerAction::Leave && hit_test)
            item = hit_test(event.position);
        String comment;
        WarningList warnings;
        if (item) {
            if (auto* object = commentable(*item))
                comment = commentText(object, is_event_view);
            if (get_warnings) warnings = get_warnings(*item);
        }
        DrawingType next_type = DrawingType::None;
        Point icon_position{};
        if (item && !comment.empty()) {
            const auto position = comment_position(*item);
            const Rect marker{position.x, position.y,
                              comment_bounds.width, comment_bounds.height};
            if (lib::comment_marker_contains(marker,event.position)) {
                next_type = DrawingType::Comment;
                icon_position = position;
            }
        }
        if (item && next_type == DrawingType::None && !warnings.empty()) {
            const auto position = warning_position(*item);
            const Rect marker{position.x, position.y,
                              warning_bounds.width, warning_bounds.height};
            if (lib::comment_marker_contains(marker,event.position)) {
                next_type = DrawingType::Warning;
                icon_position = position;
            }
        }
        if (next_type == DrawingType::None) item = nullptr;
        const auto old_item = drawing.item;
        const auto old_type = drawing.type;
        const bool same = item && old_item == item && old_type == next_type;
        if (!same) {
            drawing.item = item;
            drawing.type = next_type;
            if (old_item || item) surface.redraw();
        }

        String message;
        if (next_type == DrawingType::Comment) {
            message = comment;
        } else if (next_type == DrawingType::Warning) {
            std::sort(warnings.begin(), warnings.end());
            warnings.erase(std::unique(warnings.begin(), warnings.end()), warnings.end());
            for (const auto& warning : warnings) {
                if (!message.empty()) message.push_back('\n');
                message += warning;
            }
        }
        createOrDestroyToolTip(surface, message, surface.comment_tooltip,
                               message.empty() ? std::optional<Rect>{}
                                               : std::optional<Rect>{
                                                     {icon_position.x, icon_position.y,
                                                      next_type == DrawingType::Comment
                                                          ? comment_bounds.width
                                                          : warning_bounds.width,
                                                      next_type == DrawingType::Comment
                                                          ? comment_bounds.height
                                                          : warning_bounds.height}},
                               [&surface] { surface.redraw(); });
    };
}

void setupComment(Commons& comm, Table& table, bool is_event_view,
                  std::function<WarningList(const TableItem&)> get_warnings = {});
void setupComment(Commons& comm, Tree& tree, bool is_event_view,
                  std::function<WarningList(const TreeItem&)> get_warnings = {});

template <typename C>
void setupComment(Commons& comm, CardList<C>& list, bool is_event_view,
                  std::function<WarningList(const C&)> get_warnings = {}) {
    const Image comment_image = comm.prop.images.comment;
    const Image warning_icon = comm.prop.images.warning;
    const auto comment_bounds = comment_image.bounds();
    const auto warning_bounds = warning_icon.bounds();
    if (list.comment_comm && list.comment_repl_token)
        list.comment_comm->replText.remove(list.comment_repl_token);
    list.comment_comm = &comm;
    list.comment_repl_token = comm.replText.add([&list] { list.redraw(); });
    const auto warning_provider = get_warnings;
    list.on_paint = [&list, &comm, is_event_view, warning_provider,
                     comment_image, warning_icon, comment_bounds, warning_bounds]
                    (lib::DrawingContext& context) mutable {
        Image warning_image;
        const auto end = list.endIndex();
        const auto begin = std::min(list.showingStartIndex, end);
        for (std::size_t index = begin; index < end; ++index) {
            auto* value = list.card(index);
            if (!value) continue;
            const auto item_bounds = list.getImageBounds(index);
            const auto warnings = warning_provider ? warning_provider(*value) : WarningList{};
            const auto* object = commentable(*value);
            const auto text = commentText(object, is_event_view);
            if (!warnings.empty()) {
                if (!warning_image.valid()) {
                    const auto warning_width = std::max<std::int32_t>(
                        1, comm.prop.var.etc.warningImageWidthForCardList);
                    if (comm.prop.services.graphics)
                        warning_image = tiltWarningImage(
                            comm.prop, *comm.prop.services.graphics,
                            warning_width);
                    else
                        warning_image = tiltWarningImage(comm.prop,
                                                         warning_width);
                }
                context.draw_image(warning_image.resource, warning_image.bounds(),
                                   {item_bounds.x + item_bounds.width - warning_image.logical_size.width,
                                    item_bounds.y + item_bounds.height - warning_image.logical_size.height,
                                    warning_image.logical_size.width,
                                    warning_image.logical_size.height},
                                   lib::Interpolation::Nearest);
                const auto position = warningPos(comment_bounds, warning_bounds,
                                                 item_bounds, !text.empty());
                context.draw_image(warning_icon.resource, warning_icon.bounds(),
                                   {position.x, position.y, warning_bounds.width,
                                    warning_bounds.height}, lib::Interpolation::Nearest);
            }
            if (!text.empty()) {
                const auto position = commentPos(comment_bounds, item_bounds);
                context.draw_image(comment_image.resource, comment_image.bounds(),
                                   {position.x, position.y, comment_bounds.width,
                                    comment_bounds.height}, lib::Interpolation::Nearest);
            }
        }
    };
    setupCommentToolTip<CardList<C>, C>(
        comm, list, is_event_view, warning_provider, list.comment_drawing,
        [&list](Point point) { return list.search(point); },
        [&list, comment_bounds](const C& value) {
            return commentPos(comment_bounds, list.getImageBounds(list.indexOf(&value)));
        },
        [&list, comment_bounds, warning_bounds, is_event_view](const C& value) {
            const auto* object = commentable(value);
            return warningPos(comment_bounds, warning_bounds,
                              list.getImageBounds(list.indexOf(&value)),
                              !commentText(object, is_event_view).empty());
        });
}

class CommentDialog final : public lib::Dialog,
                            public editdialogsession::EditDialogSession,
                            public std::enable_shared_from_this<CommentDialog> {
public:
    using Container = lib::Container;
    using TextEdit = lib::TextEdit;
    using Window = lib::Window;
    using DialogResult = lib::DialogResult;

    CommentDialog(Commons& comm, std::shared_ptr<Window> parent, String comment);
    CommentDialog(Commons& comm, String comment)
        : CommentDialog(comm, {}, std::move(comment)) {}
    CommentDialog(const CommentDialog&) = delete;
    CommentDialog& operator=(const CommentDialog&) = delete;
    CommentDialog(CommentDialog&&) = delete;
    CommentDialog& operator=(CommentDialog&&) = delete;
    ~CommentDialog() override { if (clear_size_binding_) { auto clear = std::move(clear_size_binding_); clear(); } lifetime_.reset(); }

    void setup(Container& area);
    void setup();
    [[nodiscard]] String comment() const { return committed_text_; }
    [[nodiscard]] const String& text() const noexcept { return text_; }
    [[nodiscard]] const std::shared_ptr<TextEdit>& editor() const noexcept { return comment_; }
    [[nodiscard]] const std::shared_ptr<lib::Panel>& area() const noexcept { return area_; }
    [[nodiscard]] const Services& services() const noexcept { return services_; }
    [[nodiscard]] bool apply();
    // AbsDialogの変更可否は、文字列メニューの履歴操作にも適用する。
    void ignoreMod(bool value) noexcept { ignore_mod_ = value; }
    [[nodiscard]] bool ignoreMod() const noexcept { return ignore_mod_; }
    [[nodiscard]] bool catchMod() const noexcept { return !ignore_mod_; }
    [[nodiscard]] lib::Window* editDialogWindow() const noexcept override {
        return const_cast<CommentDialog*>(this);
    }
    [[nodiscard]] bool editDialogPending() const noexcept override {
        return text_ != applied_text_;
    }
    [[nodiscard]] bool applyEditDialog() override { return apply(); }
    [[nodiscard]] bool open();
    void active();
    void forceCancel();
    [[nodiscard]] bool close();
    void close(DialogResult result);
    [[nodiscard]] DialogResult result() const noexcept { return Dialog::result; }
    [[nodiscard]] bool copySelection();
    [[nodiscard]] bool cutSelection();
    [[nodiscard]] bool paste();
    void selectAll();
    void setSelection(std::size_t start, std::size_t length);
    [[nodiscard]] String selectedText() const;
    std::function<void(StringView)> applied{};
    lib::Action closed{};

private:
    std::shared_ptr<void> lifetime_{std::make_shared<int>(0)};
    std::uint64_t notification_revision_{};
    std::function<void()> clear_size_binding_{};
    // DのGCはモードレスの受信先を保持する。C++では実際の所有者があれば保持する。
    // 借用の補助経路では別の別名トークンと弱い寿命保護を使い、 生ポインターから所有
    // 権付きのenable_shared_from_this制御ブロックを作らない。
    std::shared_ptr<lib::Dialog> dialogOwner(){
        if(const auto owner=weak_from_this().lock())return owner;
        return std::shared_ptr<lib::Dialog>(std::make_shared<int>(0),this);
    }
    Commons* comm_{};
    Services services_{};
    std::shared_ptr<Window> parent_{};
    std::shared_ptr<lib::Panel> area_{};
    std::shared_ptr<TextEdit> comment_{};
    std::shared_ptr<lib::Button> apply_button_{};
    bool ignore_mod_{};
    // コメント編集のUndo/Redo履歴（Ctrl+Z/Ctrl+Y
    // ・ALLのタイトルボタン）と、文字列のコンテキストメニュー。
    std::shared_ptr<dmenu::TextMenuModify> text_menu_{};
    String text_{};
    String applied_text_{};
    // Dのcomment()は編集中の値ではなく、最後に適用した値を返す。
    String committed_text_{};
    bool initialized_{};
    bool closed_{};

    static String wrapReturnCode(StringView value);
    [[nodiscard]] lib::ClipboardBackend* clipboard() const noexcept;
    void installEditorHandlers();
};

}  // namespace cwxeditor::cwx::editor::gui::dwt::comment
