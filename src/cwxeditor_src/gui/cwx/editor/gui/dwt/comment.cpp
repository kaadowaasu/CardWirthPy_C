#include "cwxeditor_src/gui/cwx/editor/gui/dwt/comment.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dmenu.hpp"
#include "cwxeditor_src/lib/ui_window_lifecycle.hpp"
// UIの実装は一つに統一し、
// 別経路の実装を設けない。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace cwxeditor::cwx::editor::gui::dwt::comment {
namespace {

float positive(float value, float fallback) noexcept {
    return value > 0.0F ? value : fallback;
}

std::int32_t positiveWidth(const Props& prop, std::int32_t value) noexcept {
    if (value < 0) value = prop.var.etc.warningImageWidth;
    return std::max<std::int32_t>(1, value);
}

std::uint8_t channel(std::uint32_t value) noexcept {
    return static_cast<std::uint8_t>(std::min<std::uint32_t>(255, value));
}

std::uint8_t gradientAlpha(std::int32_t index, std::int32_t width,
                           std::uint8_t alpha) noexcept {
    if (width <= 0) return 0;
    const auto scaled = static_cast<float>(index) /
                        static_cast<float>(width) * static_cast<float>(alpha);
    return static_cast<std::uint8_t>(std::clamp(scaled, 0.0F, 255.0F));
}

Image makeImage(const Props& prop, std::int32_t width, std::int32_t height,
                bool tilt) {
    width = std::max<std::int32_t>(1, width);
    height = std::max<std::int32_t>(1, height);
    const auto color = prop.var.etc.warningImageColor;

    Image result;
    result.logical_size = {static_cast<float>(width), static_cast<float>(height)};
    result.data.format = lib::ImageFormat::Png;
    lib::ImageFrame frame;
    frame.logical_size = result.logical_size;
    frame.pixel_width = static_cast<std::uint32_t>(width);
    frame.pixel_height = static_cast<std::uint32_t>(height);
    frame.pixel_format = lib::PixelFormat::RGBA32;
    frame.pixels.resize(static_cast<std::size_t>(width) *
                        static_cast<std::size_t>(height) * 4U);

    const auto writePixel = [&](std::int32_t x, std::int32_t y,
                                std::uint8_t alpha) {
        const auto offset = (static_cast<std::size_t>(y) *
                             static_cast<std::size_t>(width) +
                             static_cast<std::size_t>(x)) * 4U;
        frame.pixels[offset + 0] = static_cast<std::byte>(channel(color.red));
        frame.pixels[offset + 1] = static_cast<std::byte>(channel(color.green));
        frame.pixels[offset + 2] = static_cast<std::byte>(channel(color.blue));
        frame.pixels[offset + 3] = static_cast<std::byte>(alpha);
    };

    for (std::int32_t y = 0; y < height; ++y) {
        for (std::int32_t x = 0; x < width; ++x) {
            std::uint8_t alpha = 0;
            if (!tilt) {
                alpha = gradientAlpha(x, width, channel(color.alpha));
            } else if (y == height - 1) {
                alpha = gradientAlpha(x, width, channel(color.alpha));
            } else {
                const auto start = width - y;
                if (x >= start)
                    alpha = gradientAlpha(x - start, width, channel(color.alpha));
            }
            writePixel(x, y, alpha);
        }
    }
    result.data.frames.push_back(std::move(frame));
    return result;
}

void registerImage(Image& image, lib::GraphicsBackend* graphics) {
    if (!graphics || image.data.frames.empty()) return;
    image.resource = graphics->create_image(image.data);
}

Rect tableRowBounds(const Table& table, std::size_t index) noexcept {
    if (index < table.row_bounds.size()) return table.row_bounds[index];
    const auto client = table.clientBounds();
    const auto height = positive(table.row_height,
                                 static_cast<float>(table.comment_comm
                                     ? table.comment_comm->prop.var.etc.rowHeight
                                     : 20U));
    return {client.x, client.y + static_cast<float>(index) * height,
            client.width, height};
}

Rect treeRowBounds(const Tree& tree, std::size_t index) noexcept {
    const auto client = tree.bounds();
    const auto height = positive(tree.row_height, 20.0F);
    return {client.x, client.y + static_cast<float>(index) * height,
            client.width, height};
}

float tableContentWidth(const Table& table) noexcept {
    float width = 0.0F;
    for (const auto& column : table.columns)
        width += std::max(0.0F, column.width);
    return width;
}

bool hasExpandedAncestor(const Tree& tree, const lib::TreeNode& node) noexcept {
    auto parent = node.parent;
    while (parent != 0) {
        const auto found = std::find_if(tree.nodes.begin(), tree.nodes.end(),
                                        [parent](const lib::TreeNode& candidate) {
                                            return candidate.id == parent;
                                        });
        if (found == tree.nodes.end()) return false;
        if (!found->expanded) return false;
        parent = found->parent;
    }
    return true;
}

}  // namespace

bool TableItem::isDisposed() const noexcept {
    return disposed || !owner || owner->disposed;
}

bool TreeItem::isDisposed() const noexcept {
    return disposed || !owner || owner->disposed;
}

TableItem Table::item(const std::size_t index) noexcept {
    return {this, index,
            index < item_paths.size() ? item_paths[index] : nullptr,
            tableRowBounds(*this, index), false};
}

TableItem& Table::commentItem(const std::size_t index) noexcept {
    if (comment_items.size() <= index) comment_items.resize(index + 1U);
    comment_items[index] = item(index);
    return comment_items[index];
}

void Table::redraw() noexcept {
    ++redraw_revision;
    if (on_redraw) on_redraw();
}

void Table::redraw(Rect) noexcept { redraw(); }

TreeItem Tree::item(const lib::TreeItemId id) noexcept {
    const auto found = std::find_if(nodes.begin(), nodes.end(),
                                    [id](const lib::TreeNode& node) {
                                        return node.id == id;
                                    });
    if (found == nodes.end()) return {this, id, nullptr, {}, false};
    const auto index = static_cast<std::size_t>(found - nodes.begin());
    return {this, id, index < item_paths.size() ? item_paths[index] : nullptr,
            treeRowBounds(*this, index), false};
}

TreeItem& Tree::commentItem(const lib::TreeItemId id) noexcept {
    const auto found = std::find_if(comment_items.begin(), comment_items.end(),
                                    [id](const TreeItem& value) { return value.id == id; });
    if (found != comment_items.end()) {
        *found = item(id);
        return *found;
    }
    comment_items.push_back(item(id));
    return comment_items.back();
}

std::vector<TreeItem> Tree::visibleItems() noexcept {
    std::vector<TreeItem> result;
    result.reserve(nodes.size());
    for (const auto& node : nodes) {
        if (!hasExpandedAncestor(*this, node)) continue;
        result.push_back(item(node.id));
    }
    const auto begin = std::min(top_index, result.size());
    const auto end = visible_rows == 0
                         ? result.size()
                         : std::min(result.size(), begin + visible_rows);
    if (begin == 0 && end == result.size()) return result;
    return {result.begin() + static_cast<std::ptrdiff_t>(begin),
            result.begin() + static_cast<std::ptrdiff_t>(end)};
}

void Tree::redraw() noexcept {
    ++redraw_revision;
    if (on_redraw) on_redraw();
}

void Tree::redraw(Rect) noexcept { redraw(); }

String commentText(const PathObject* object, const bool is_event_view) {
    if (!object) return {};
    if (is_event_view) {
        if (const auto* owner = dynamic_cast<const ::cwxeditor::cwx::event::EventTreeOwner*>(object))
            return owner->commentForEvents();
    }
    if (const auto* comment = dynamic_cast<const ::cwxeditor::cwx::path::Commentable*>(object))
        return comment->comment();
    return {};
}

Point commentPos(const Rect& comment_bounds, const TableItem& item) {
    if (!item.owner) return commentPos(comment_bounds, item.bounds());
    auto client = item.owner->clientBounds();
    const auto content_width = tableContentWidth(*item.owner);
    if (content_width > 0.0F) client.width = std::min(client.width, content_width);
    const auto marker = lib::comment_marker_bounds(
        {client.x,item.bounds().y,client.width,item.bounds().height},
        {comment_bounds.width,comment_bounds.height},5.0F,
        lib::CommentMarkerPlacement::CenterRight);
    return {marker.x,marker.y};
}

Point commentPos(const Rect& comment_bounds, const TreeItem& item) {
    if (!item.owner) return commentPos(comment_bounds, item.bounds());
    const auto client = item.owner->bounds();
    const auto marker = lib::comment_marker_bounds(
        {client.x,item.bounds().y,client.width,item.bounds().height},
        {comment_bounds.width,comment_bounds.height},5.0F,
        lib::CommentMarkerPlacement::CenterRight);
    return {marker.x,marker.y};
}

Point commentPos(const Rect& comment_bounds, const Rect& item_bounds) {
    const auto marker = lib::comment_marker_bounds(
        item_bounds,{comment_bounds.width,comment_bounds.height},5.0F,
        lib::CommentMarkerPlacement::BottomRight);
    return {marker.x,marker.y};
}

Point warningPos(const Rect& comment_bounds, const Rect& warning_bounds,
                 const TableItem& item, const bool is_event_view) {
    if (!item.owner) return warningPos(comment_bounds, warning_bounds, item.bounds(),
                                       !commentText(commentable(item), is_event_view).empty());
    auto client = item.owner->clientBounds();
    const auto content_width = tableContentWidth(*item.owner);
    if (content_width > 0.0F) client.width = std::min(client.width, content_width);
    auto result = Point{client.x + client.width - warning_bounds.width - 5.0F,
                        item.bounds().y + (item.bounds().height - warning_bounds.height) / 2.0F};
    if (!commentText(commentable(item), is_event_view).empty())
        result.x -= comment_bounds.width + 5.0F;
    return result;
}

Point warningPos(const Rect& comment_bounds, const Rect& warning_bounds,
                 const TreeItem& item, const bool is_event_view) {
    if (!item.owner) return warningPos(comment_bounds, warning_bounds, item.bounds(),
                                       !commentText(commentable(item), is_event_view).empty());
    const auto client = item.owner->bounds();
    auto result = Point{client.x + client.width - warning_bounds.width - 5.0F,
                        item.bounds().y + (item.bounds().height - warning_bounds.height) / 2.0F};
    if (!commentText(commentable(item), is_event_view).empty())
        result.x -= comment_bounds.width + 5.0F;
    return result;
}

Point warningPos(const Rect& comment_bounds, const Rect& warning_bounds,
                 const Rect& item_bounds, const bool has_comment) {
    auto result = Point{item_bounds.x + item_bounds.width - warning_bounds.width - 5.0F,
                        item_bounds.y + item_bounds.height - warning_bounds.height - 5.0F};
    if (has_comment) result.y -= comment_bounds.height + 5.0F;
    return result;
}

PathObject* commentable(TableItem& item) noexcept { return item.value; }
const PathObject* commentable(const TableItem& item) noexcept { return item.value; }
PathObject* commentable(TreeItem& item) noexcept { return item.value; }
const PathObject* commentable(const TreeItem& item) noexcept { return item.value; }

Image tiltWarningImage(const Props& prop, const std::int32_t warning_image_width) {
    const auto width = positiveWidth(prop, warning_image_width);
    return makeImage(prop, width, width, true);
}

Image tiltWarningImage(const Props& prop, lib::GraphicsBackend& graphics,
                       const std::int32_t warning_image_width) {
    auto image = tiltWarningImage(prop, warning_image_width);
    registerImage(image, &graphics);
    return image;
}

void createOrDestroyToolTip(lib::Widget& parent, const StringView message,
                            std::shared_ptr<ToolTip>& tool_tip,
                            const std::optional<Rect> icon_rect,
                            lib::Action mouse_track) {
    if (message.empty()) {
        parent.set_tooltip({});
        parent.set_tooltip_balloon(false);
        parent.set_tooltip_immediate(false);
        if (tool_tip) {
            tool_tip->visible = false;
            if (tool_tip->widget) tool_tip->widget->set_visible(false);
            tool_tip->message.clear();
        }
        return;
    }
    if (!tool_tip) tool_tip = std::make_shared<ToolTip>();
    tool_tip->message.assign(message.data(), message.size());
    parent.set_tooltip(tool_tip->message);
    parent.set_tooltip_balloon(true);
    parent.set_tooltip_immediate(true);
    tool_tip->icon_rect = icon_rect.value_or(Rect{});
    tool_tip->location = {tool_tip->icon_rect.x,
                          tool_tip->icon_rect.y + tool_tip->icon_rect.height + 15.0F};
    tool_tip->visible = true;
    tool_tip->mouse_track = std::move(mouse_track);
    if (!tool_tip->widget) tool_tip->widget = std::make_shared<lib::ToolTip>();
    tool_tip->widget->text = tool_tip->message;
    tool_tip->widget->balloon = true;
    tool_tip->widget->set_bounds({tool_tip->location.x, tool_tip->location.y, 0.0F, 0.0F});
    tool_tip->widget->set_visible(true);
}

void createOrDestroyToolTip(lib::Widget& parent, const StringView message,
                            std::unique_ptr<ToolTip>& tool_tip,
                            const std::optional<Rect> icon_rect,
                            lib::Action mouse_track) {
    if (message.empty()) {
        parent.set_tooltip({});
        parent.set_tooltip_balloon(false);
        parent.set_tooltip_immediate(false);
        if (tool_tip) {
            tool_tip->visible = false;
            if (tool_tip->widget) tool_tip->widget->set_visible(false);
            tool_tip->message.clear();
        }
        return;
    }
    if (!tool_tip) tool_tip = std::make_unique<ToolTip>();
    tool_tip->message.assign(message.data(), message.size());
    parent.set_tooltip(tool_tip->message);
    parent.set_tooltip_balloon(true);
    parent.set_tooltip_immediate(true);
    tool_tip->icon_rect = icon_rect.value_or(Rect{});
    tool_tip->location = {tool_tip->icon_rect.x,
                          tool_tip->icon_rect.y + tool_tip->icon_rect.height + 15.0F};
    tool_tip->visible = true;
    tool_tip->mouse_track = std::move(mouse_track);
    if (!tool_tip->widget) tool_tip->widget = std::make_shared<lib::ToolTip>();
    tool_tip->widget->text = tool_tip->message;
    tool_tip->widget->balloon = true;
    tool_tip->widget->set_bounds({tool_tip->location.x, tool_tip->location.y, 0.0F, 0.0F});
    tool_tip->widget->set_visible(true);
}

void setupComment(Commons& comm, Table& table, const bool is_event_view,
                  std::function<WarningList(const TableItem&)> get_warnings) {
    const auto comment_image = comm.prop.images.comment;
    const auto warning_icon = comm.prop.images.warning;
    const auto comment_bounds = comment_image.bounds();
    const auto warning_bounds = warning_icon.bounds();
    if (table.comment_comm && table.comment_repl_token)
        table.comment_comm->replText.remove(table.comment_repl_token);
    table.comment_comm = &comm;
    table.comment_repl_token = comm.replText.add([&table] { table.redraw(); });
    const auto warning_provider = get_warnings;
    table.on_paint = [&table, &comm, is_event_view, warning_provider,
                      comment_image, warning_icon, comment_bounds, warning_bounds]
                     (lib::DrawingContext& context) mutable {
        Image warning_image;
        const auto count = table.rows.size();
        const auto begin = std::min(table.top_index, count);
        const auto end = table.visible_rows == 0
                             ? count
                             : std::min(count, begin + table.visible_rows);
        for (std::size_t index = begin; index < end; ++index) {
            auto item = table.item(index);
            if (item.isDisposed()) continue;
            const auto item_bounds = item.bounds();
            if (table.clientBounds().y + table.clientBounds().height <=
                item_bounds.y)
                break;
            if (item_bounds.y + item_bounds.height <= table.clientBounds().y)
                continue;
            const auto warnings = warning_provider ? warning_provider(item) : WarningList{};
            const auto client = table.clientBounds();
            const auto content_width = tableContentWidth(table);
            drawCommentAndWarning(comm.prop, is_event_view, comment_image,
                                   comment_bounds, warning_icon, warning_bounds,
                                   client.x, content_width > 0.0F
                                                ? std::min(client.width, content_width)
                                                : client.width,
                                   item, context, table.comment_drawing, warnings,
                                   comm.prop.var.etc.warningImageWidthForTable,
                                   warning_image);
        }
    };
    setupCommentToolTip<Table, TableItem>(
        comm, table, is_event_view, warning_provider, table.comment_drawing,
        [&table](Point point) -> TableItem* {
            const auto count = table.rows.size();
            const auto begin = std::min(table.top_index, count);
            const auto end = table.visible_rows == 0
                                 ? count
                                 : std::min(count, begin + table.visible_rows);
            for (std::size_t index = begin; index < end; ++index) {
                auto& item = table.commentItem(index);
                const auto b = item.bounds();
                if (b.x <= point.x && point.x < b.x + b.width &&
                    b.y <= point.y && point.y < b.y + b.height)
                    return &item;
            }
            return nullptr;
        },
        [comment_bounds](const TableItem& item) {
            return commentPos(comment_bounds, item);
        },
        [comment_bounds, warning_bounds, is_event_view](const TableItem& item) {
            return warningPos(comment_bounds, warning_bounds, item, is_event_view);
        });
}

void setupComment(Commons& comm, Tree& tree, const bool is_event_view,
                  std::function<WarningList(const TreeItem&)> get_warnings) {
    const auto comment_image = comm.prop.images.comment;
    const auto warning_icon = comm.prop.images.warning;
    const auto comment_bounds = comment_image.bounds();
    const auto warning_bounds = warning_icon.bounds();
    if (tree.comment_comm && tree.comment_repl_token)
        tree.comment_comm->replText.remove(tree.comment_repl_token);
    tree.comment_comm = &comm;
    tree.comment_repl_token = comm.replText.add([&tree] { tree.redraw(); });
    const auto warning_provider = get_warnings;
    tree.on_paint = [&tree, &comm, is_event_view, warning_provider,
                     comment_image, warning_icon, comment_bounds, warning_bounds]
                    (lib::DrawingContext& context) mutable {
        Image warning_image;
        for (const auto& item : tree.visibleItems()) {
            if (item.isDisposed()) continue;
            const auto warnings = warning_provider ? warning_provider(item) : WarningList{};
            drawCommentAndWarning(comm.prop, is_event_view, comment_image,
                                   comment_bounds, warning_icon, warning_bounds,
                                   tree.bounds().x, tree.bounds().width, item,
                                   context, tree.comment_drawing, warnings,
                                   comm.prop.var.etc.warningImageWidthForTree,
                                   warning_image);
        }
    };
    setupCommentToolTip<Tree, TreeItem>(
        comm, tree, is_event_view, warning_provider, tree.comment_drawing,
        [&tree](Point point) -> TreeItem* {
            for (const auto& visible : tree.visibleItems()) {
                auto& item = tree.commentItem(visible.id);
                const auto b = item.bounds();
                if (b.x <= point.x && point.x < b.x + b.width &&
                    b.y <= point.y && point.y < b.y + b.height)
                    return &item;
            }
            return nullptr;
        },
        [comment_bounds](const TreeItem& item) {
            return commentPos(comment_bounds, item);
        },
        [comment_bounds, warning_bounds, is_event_view](const TreeItem& item) {
            return warningPos(comment_bounds, warning_bounds, item, is_event_view);
        });
}

void bindEditorImages(Props& target, dprops::Props& editor) {
    target.images.comment.resource = editor.images().menu(::cwxeditor::cwx::types::MenuID::Comment);
    target.images.warning.resource = editor.images().warning();
}

CommentDialog::CommentDialog(Commons& comm, std::shared_ptr<Window> parent,
                             String comment)
    : comm_(&comm), services_(comm.effectiveServices()), parent_(std::move(parent)),
      text_(std::move(comment)), applied_text_(text_), committed_text_(text_) {
    title = comm.prop.msgs.dlgTitComment;
    icon = comm.prop.images.menu(::cwxeditor::cwx::types::MenuID::Comment).resource;
    lib::configure_owned_window(*this, parent_,
        lib::OwnedWindowKind::Modeless);
    Dialog::result = DialogResult::None;
    resizable = true;
    on_close_requested = [this,life=std::weak_ptr<void>(lifetime_)] { if(!life.expired())close(DialogResult::Cancel); };
}

String CommentDialog::wrapReturnCode(const StringView value) {
    String result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '\r' && index + 1 < value.size() &&
            value[index + 1] == '\n') continue;
        if (value[index] == '\r') { result.push_back('\n'); continue; }
        result.push_back(value[index]);
    }
    return result;
}

lib::ClipboardBackend* CommentDialog::clipboard() const noexcept {
    return services_.clipboard.get();
}

void CommentDialog::setup(Container& area) {
    if (!comment_) {
        lib::WidgetPtr widget;
        if (services_.gui)
            widget = services_.gui->create_component(lib::ComponentKind::TextEdit);
        comment_ = std::dynamic_pointer_cast<TextEdit>(std::move(widget));
        if (!comment_) comment_ = std::make_shared<TextEdit>();
    }
    auto layout = std::make_shared<lib::GridLayout>();
    layout->columns = 1U;
    area.set_layout(layout);
    const auto& children = area.children();
    if (std::find(children.begin(), children.end(), comment_) == children.end())
        area.add(comment_);
    comment_->multiline = true;
    comment_->wrap = true;
    comment_->read_only = false;
    comment_->tab_width=comm_ ? comm_->prop.var.etc.textTabs:4U;
    if (comm_) comment_->set_font(comm_->prop.looks.textDlgFont(10.0F));
    comment_->text = text_;
    comment_->selection_start = comment_->text.size();
    comment_->selection_length = 0;
    comment_->set_layout_item({0U, 0U, 1U, 1U, lib::Align::Stretch,
                               lib::Align::Stretch, {}, {}, {}, 1.0F});
    comment_->on_changed = [this,life=std::weak_ptr<void>(lifetime_)](StringView value) {
        if(life.expired())return;
        text_.assign(value.data(), value.size());
        if(catchMod() && apply_button_)apply_button_->set_enabled(true);
    };
    installEditorHandlers();
    // 標準テキストメニューcreateTextMenuが取り消し履歴を提供
    // し、入力・切り取り・貼り付けをCtrl+Z/Ctrl+Yで戻せるよ
    // うにする。ALLのタイトル部の元に戻す・やり直しも同じキーを送る。
    // installEditorHandlersの後で設定してコールバッ
    // クを包み、ダイアログ自身のCtrl+A/C/X/Vは先に処理する。
    {
        dmenu::Commons text_comm;
        text_comm.services = services_;
        // 既存の共通Comment専用のタブ設定を維持する。Dは
        // 先にtabsを設定しても後の汎用メニューがtextTabs
        // で上書きするため、意図的に修正する。専用設定を同じ共
        // 通メニュー補助処理へ渡し、別のUI経路は作らない。
        text_comm.presentation.textTabs = comment_->tab_width;
        text_menu_ = dmenu::createTextMenu(text_comm, dmenu::TMM(comment_),
            [this, life=std::weak_ptr<void>(lifetime_)] {
                return !life.expired() && catchMod();
            });
    }

    auto buttons = std::make_shared<lib::Panel>();
    auto button_layout = std::make_shared<lib::GridLayout>();
    button_layout->columns = 3U;
    buttons->set_layout(button_layout);
    const auto make_button = [this, &buttons](std::string text,
                                              lib::Action action) {
        std::shared_ptr<lib::Button> button;
        if (services_.gui)
            button = std::dynamic_pointer_cast<lib::Button>(
                services_.gui->create_component(lib::ComponentKind::Button));
        if (!button) button = std::make_shared<lib::Button>();
        button->text = std::move(text);
        button->on_click = std::move(action);
        buttons->add(button);
        return button;
    };
    auto ok = make_button(comm_ ? comm_->prop.msgs.dlgTextOK : "OK",
                          [this,life=std::weak_ptr<void>(lifetime_)] { if(!life.expired() && apply() && !life.expired())close(DialogResult::OK); });
    ok->default_button = true;
    auto cancel = make_button(
        comm_ ? comm_->prop.msgs.dlgTextCancel : "Cancel",
        [this,life=std::weak_ptr<void>(lifetime_)] { if(!life.expired())close(DialogResult::Cancel); });
    cancel->cancel_button = true;
    apply_button_ = make_button(
        comm_ ? comm_->prop.msgs.dlgTextApply : "Apply",
        [this,life=std::weak_ptr<void>(lifetime_)] { if(!life.expired())static_cast<void>(apply()); });
    apply_button_->set_enabled(false);
    area.add(buttons);
    initialized_ = true;
}

void CommentDialog::setup() {
    if (!area_) {
        area_ = std::make_shared<lib::Panel>();
        add(area_);
    }
    setup(*area_);
}

void CommentDialog::installEditorHandlers() {
    if (!comment_) return;
    comment_->on_key_down = [this,life=std::weak_ptr<void>(lifetime_)](lib::KeyEvent& event) {
        if(life.expired())return;
        const auto modifiers = static_cast<std::uint32_t>(event.modifiers);
        const bool control = (modifiers & static_cast<std::uint32_t>(lib::Modifiers::Control)) != 0;
        if (!control) return;
        if (event.key == lib::Key::A) { selectAll(); event.handled = true; }
        else if (event.key == lib::Key::C) event.handled = copySelection();
        else if (event.key == lib::Key::X) event.handled = cutSelection();
        else if (event.key == lib::Key::V) event.handled = paste();
    };
    comment_->on_text_input = [this,life=std::weak_ptr<void>(lifetime_)](lib::TextInputEvent& event) {
        if(life.expired())return;
        if (event.composing || event.text.empty() || !comment_ || comment_->read_only) return;
        const auto start = std::min(comment_->selection_start, comment_->text.size());
        const auto length = std::min(comment_->selection_length,
                                     comment_->text.size() - start);
        comment_->text.replace(start, length, event.text);
        comment_->selection_start = start + event.text.size();
        comment_->selection_length = 0;
        text_ = comment_->text;
        if (comment_->on_changed) comment_->on_changed(comment_->text);
        event.handled = true;
    };
}

bool CommentDialog::apply(){
    const auto owner=dialogOwner();const std::weak_ptr<void> life=lifetime_;
    const auto revision=++notification_revision_;
    const auto valid=[&]{return !life.expired() && notification_revision_==revision && !closed_;};
    if(closed_)return false;
    const auto next=::cwxeditor::cwx::utils::lastRet(wrapReturnCode(comment_ ? comment_->text:text_));text_=next;committed_text_=next;if(comment_)comment_->text=next;
    const auto callback=applied;auto* comm=comm_;
    if(comm)comm->replText.notify();if(!valid())return false;
    if(callback)callback(next);if(!valid())return false;
    applied_text_=next;
    if(apply_button_)apply_button_->set_enabled(false);
    return true;
}

bool CommentDialog::open(){
    const auto owner=dialogOwner();const std::weak_ptr<void> life=lifetime_;
    if(!initialized_)setup();if(life.expired())return false;
    if (!clear_size_binding_ && comm_->prop.bindDialogSize) {
        clear_size_binding_ = comm_->prop.bindDialogSize(owner);
        if (life.expired()) return false;
    }
    closed_=false;const auto revision=++notification_revision_;const auto gui=services_.gui;const auto parent=parent_;const bool is_modal=modal;
    lib::DialogResult response=DialogResult::None;
    if(gui){if(is_modal)response=gui->show_modal(owner,parent);else gui->show(owner);}else set_visible(true);
    if(life.expired() || notification_revision_!=revision)return false;
    if(is_modal)Dialog::result=response;
    if(Dialog::result!=DialogResult::None)closed_=true;
    return !closed_;
}
void CommentDialog::active(){
    if(closed_)return;
    const auto owner=dialogOwner();const std::weak_ptr<void> life=lifetime_;const auto revision=notification_revision_;
    const auto gui=services_.gui;const auto editor=comment_;
    if(gui){gui->show(owner);if(life.expired() || notification_revision_!=revision || closed_)return;if(editor)gui->focus(editor);}
    else set_visible(true);
}
void CommentDialog::forceCancel(){close(DialogResult::Cancel);}
bool CommentDialog::close(){const std::weak_ptr<void> life=lifetime_;close(DialogResult::Cancel);return life.expired() || closed_;}
void CommentDialog::close(const DialogResult value){
    if(closed_)return;
    const auto owner=dialogOwner();const std::weak_ptr<void> life=lifetime_;const auto revision=++notification_revision_;
    if (clear_size_binding_) { auto clear = std::move(clear_size_binding_); clear(); }
    Dialog::result=value;closed_=true;const auto gui=services_.gui;const auto callback=closed;
    if(gui)gui->close(owner);else set_visible(false);
    if(life.expired() || notification_revision_!=revision)return;
    if(callback)callback();
}

void CommentDialog::selectAll() {
    if (!comment_) return;
    comment_->selection_start = 0;
    comment_->selection_length = comment_->text.size();
}

void CommentDialog::setSelection(std::size_t start, std::size_t length) {
    if (!comment_) return;
    comment_->selection_start = std::min(start, comment_->text.size());
    comment_->selection_length = std::min(length,
                                          comment_->text.size() - comment_->selection_start);
}

String CommentDialog::selectedText() const {
    if (!comment_) return {};
    const auto start = std::min(comment_->selection_start, comment_->text.size());
    const auto length = std::min(comment_->selection_length,
                                 comment_->text.size() - start);
    return comment_->text.substr(start, length);
}

bool CommentDialog::copySelection() {
    auto* backend = clipboard();
    const auto selected = selectedText();
    if (!backend || selected.empty()) return false;
    if (comm_->prop.var.etc.xmlCopy && selected.rfind("<?xml ", 0) == 0) {
        xmlbytestransfer::ClipData clip(*backend);
        xmlbytestransfer::XMLtoCB(true, clip, selected);
    } else {
        lib::ClipboardData data;
        data.text = selected;
        backend->clear();
        backend->write(data);
    }
    return true;
}

bool CommentDialog::cutSelection() {
    if (!comment_ || comment_->read_only || !copySelection()) return false;
    const auto start = std::min(comment_->selection_start, comment_->text.size());
    const auto length = std::min(comment_->selection_length,
                                 comment_->text.size() - start);
    comment_->text.erase(start, length);
    comment_->selection_start = start;
    comment_->selection_length = 0;
    text_ = comment_->text;
    // 取り消し履歴へ記録されるよう、編集を公開する。
    if (comment_->on_changed) comment_->on_changed(comment_->text);
    return true;
}

bool CommentDialog::paste() {
    auto* backend = clipboard();
    if (!backend || !comment_ || comment_->read_only) return false;
    const auto data = backend->read();
    String value = data.text;
    if (value.empty()) value = xmlbytestransfer::CBtoXML(data);
    if (value.empty()) return false;
    const auto start = std::min(comment_->selection_start, comment_->text.size());
    const auto length = std::min(comment_->selection_length,
                                 comment_->text.size() - start);
    comment_->text.replace(start, length, value);
    comment_->selection_start = start + value.size();
    comment_->selection_length = 0;
    text_ = comment_->text;
    // 取り消し履歴へ記録されるよう、編集を公開する。
    if (comment_->on_changed) comment_->on_changed(comment_->text);
    return true;
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::comment
