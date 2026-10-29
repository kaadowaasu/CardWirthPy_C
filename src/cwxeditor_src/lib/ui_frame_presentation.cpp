#include "cwxeditor_src/lib/editor_diagnostics.hpp"
#include "ui_frame_presentation.hpp"
#include "drawing_clip.hpp"
#include "ui_metrics.hpp"
#include <algorithm>
#include <iterator>
#include "ui_inline_edit.hpp"
#include "ui_layout.hpp"
#include "ui_child_order.hpp"
#include "ui_window_stack.hpp"
#include <unordered_set>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>

namespace cwxeditor::lib {
namespace {
bool nonempty(Rect r) { return r.width>0 && r.height>0; }
bool same_color(const std::optional<Color>& a,const std::optional<Color>& b) {
    return a.has_value()==b.has_value() && (!a ||
        (a->red==b->red && a->green==b->green && a->blue==b->blue && a->alpha==b->alpha));
}
bool same_font(const std::optional<FontSpec>& a,const std::optional<FontSpec>& b) {
    return a.has_value()==b.has_value() && (!a ||
        (a->family==b->family && a->points==b->points && a->weight==b->weight &&
         a->italic==b->italic && a->underline==b->underline && a->strikeout==b->strikeout &&
         a->antialias==b->antialias && a->size_mode==b->size_mode));
}
std::vector<WidgetPtr> frame_children(const WidgetPtr& widget) {
    if (const auto tabs=std::dynamic_pointer_cast<TabView>(widget)) {
        if (tabs->selected_index && *tabs->selected_index<tabs->pages.size())
            return {tabs->pages[*tabs->selected_index].content};
        return {};
    }
    if (const auto expand=std::dynamic_pointer_cast<ExpandBar>(widget)) {
        std::vector<WidgetPtr> result;
        for (std::size_t i=0;i<expand->children().size();++i)
            if (expand_item_is_open(*expand,i)) result.push_back(expand->children()[i]);
        return result;
    }
    if (const auto container=std::dynamic_pointer_cast<Container>(widget)) {
        // Canvas の描画はモデル走査後に記録するが、再
        // 生は表示順に行う。ここでも同じ共通の順序を使う。
        // 前景の兄弟より後に定義した背景 Canvas が、
        // コールバックでは後、描画では先になると、 副作用・
        // リソースが異なる論理フレームを作ってしまうため。
        return presentation_children(*container);
    }
    return {};
}
// スクロール位置の制限は描画ではなくモデルの同期なので、FrameStructureSnapshot
// が世代を確定する前に完了する。 縮小した一覧・表・ツリ
// ーの初回描画が自身のスナップショットを無効にし、 無関係
// な選択タブ・展開内容までそのフレームから消すことを防ぐ。
void reconcile_item_view_scroll(const WidgetPtr& root,const float row_height) {
    std::unordered_set<const Widget*> visited;
    const auto visit=[&](auto&& self,const WidgetPtr& widget)->void {
        if (!widget || !widget->visible() || !visited.insert(widget.get()).second) return;
        if (auto* list=dynamic_cast<ListView*>(widget.get())) {
            const auto scroll=item_scroll_geometry(*list,row_height);
            (void)set_item_view_top_index(*list,list->top_index,scroll.visible_rows);
        } else if (auto* table=dynamic_cast<TableView*>(widget.get())) {
            (void)set_table_horizontal_offset(*table,table->horizontal_offset);
            const auto scroll=item_scroll_geometry(*table,row_height);
            (void)set_item_view_top_index(*table,table->top_index,scroll.visible_rows);
        } else if (auto* tree=dynamic_cast<TreeView*>(widget.get())) {
            (void)set_item_horizontal_offset(*tree,tree->horizontal_offset);
            (void)set_item_view_top_index(*tree,tree->top_index,
                                          tree_visible_row_count(*tree,row_height));
        }
        for (const auto& child:frame_children(widget)) self(self,child);
    };
    visit(visit,root);
}
}
void FrameAnimationSchedule::request_tick(std::uint64_t now, std::uint64_t period) {
    if (!period) return;
    const auto remaining=period-now%period;
    if (remaining>std::numeric_limits<std::uint64_t>::max()-now) return;
    const auto deadline=now+remaining;
    request_at(deadline);
}
void FrameAnimationSchedule::request_at(std::uint64_t deadline) {
    if (!next_update || deadline<*next_update) next_update=deadline;
}
void FrameStructureSnapshot::capture(const std::shared_ptr<Window>& window) {
    const EditorPerformanceScope performance("frame.structure.capture");
    entries_.clear(); scale_=editor_ui_text_scale();
    std::unordered_set<const Widget*> visited;
    const auto visit=[&](auto&& self,const WidgetPtr& widget)->void {
        if (!widget || !visited.insert(widget.get()).second) return;
        auto children=widget->visible() ? frame_children(widget) : std::vector<WidgetPtr>{};
        entries_.push_back({widget,widget->bounds(),widget->visible(),widget->enabled(),{}, {},children});
        auto& entry=entries_.back();
        entry.font=widget->font(); entry.foreground=widget->foreground();
        entry.background=widget->background(); entry.cursor=widget->cursor();
        if (const auto text=std::dynamic_pointer_cast<TextWidget>(widget)) entry.text=text->text;
        if (const auto group=std::dynamic_pointer_cast<GroupBox>(widget)) entry.title=group->title;
        if (const auto list=std::dynamic_pointer_cast<ListView>(widget)) {
            entry.list_items=list->items; entry.index_selection=list->selection;
            entry.index_anchor=list->selection_anchor; entry.index_cursor=list->selection_cursor;
            entry.top_index=list->top_index; entry.model_update_revision=list->item_update_revision;
            entry.model_order_revision=list->item_order_revision; entry.scroll_revision=list->scroll_update_revision;
            entry.multi_select=list->multi_select; entry.checkable=list->checkable;
        } else if (const auto table=std::dynamic_pointer_cast<TableView>(widget)) {
            entry.table_columns=table->columns; entry.table_rows=table->rows;
            entry.index_selection=table->selection; entry.index_anchor=table->selection_anchor;
            entry.index_cursor=table->selection_cursor; entry.top_index=table->top_index;
            entry.horizontal_offset=table->horizontal_offset;
            entry.model_update_revision=table->row_update_revision;
            entry.model_order_revision=table->row_order_revision; entry.scroll_revision=table->scroll_update_revision;
            entry.row_height=table->row_height; entry.header_height=table->header_height;
            entry.highlighted_column=table->highlighted_column; entry.inline_edit_column=table->inline_edit_column;
            entry.multi_select=table->multi_select; entry.checkable=table->checkable;
            entry.full_row_selection=table->full_row_selection; entry.virtualized=table->virtualized;
            entry.check_selected_rows=table->check_selected_rows;
            entry.edit_on_double_click=table->edit_on_double_click;
            entry.header_visible=table->header_visible; entry.grid_lines=table->grid_lines;
        } else if (const auto tree=std::dynamic_pointer_cast<TreeView>(widget)) {
            entry.tree_nodes=tree->nodes; entry.tree_selection=tree->selection;
            entry.tree_anchor=tree->selection_anchor; entry.tree_cursor=tree->selection_cursor;
            entry.top_index=tree->top_index; entry.horizontal_offset=tree->horizontal_offset;
            entry.model_update_revision=tree->node_update_revision;
            entry.model_order_revision=tree->node_order_revision; entry.scroll_revision=tree->scroll_update_revision;
            entry.multi_select=tree->multi_select; entry.checkable=tree->checkable;
            entry.show_expanders=tree->show_expanders; entry.flush_root_left=tree->flush_root_left;
        }
        if (const auto tabs=std::dynamic_pointer_cast<TabView>(widget)) {
            entry.tab=snapshot_tab_layout(*tabs);
            entry.tab_page_update_revision=tabs->page_update_revision;
            entry.tab_page_order_revision=tabs->page_order_revision;
        }
        for (const auto& child:children) self(self,child);
    };
    visit(visit,window);
}
bool FrameStructureSnapshot::current() const {
    const EditorPerformanceScope performance("frame.structure.validate");
    if (entries_.empty() || scale_!=editor_ui_text_scale()) return false;
    for (const auto& entry:entries_) {
        const auto& w=entry.widget;
        if (entry.tab && !tab_layout_is_current(*entry.tab,*std::static_pointer_cast<TabView>(w)))
            return false;
        const auto r=w->bounds(), old=entry.bounds;
        const auto text=std::dynamic_pointer_cast<TextWidget>(w);
        const auto group=std::dynamic_pointer_cast<GroupBox>(w);
        if ((entry.text && (!text || *entry.text!=text->text)) ||
            (entry.title && (!group || *entry.title!=group->title)) ||
            !same_font(entry.font,w->font()) || !same_color(entry.foreground,w->foreground()) ||
            !same_color(entry.background,w->background()) || entry.cursor!=w->cursor() ||
            entry.visible!=w->visible() || entry.enabled!=w->enabled() ||
            r.x!=old.x || r.y!=old.y || r.width!=old.width || r.height!=old.height ||
            (w->visible() && entry.children!=frame_children(w))) return false;
        if (entry.list_items) {
            const auto list=std::dynamic_pointer_cast<ListView>(w);
            if (!list || *entry.list_items!=list->items || entry.index_selection!=list->selection ||
                entry.index_anchor!=list->selection_anchor || entry.index_cursor!=list->selection_cursor ||
                entry.top_index!=list->top_index || entry.model_update_revision!=list->item_update_revision ||
                entry.model_order_revision!=list->item_order_revision || entry.scroll_revision!=list->scroll_update_revision ||
                entry.multi_select!=list->multi_select || entry.checkable!=list->checkable)
                return false;
        }
        if (entry.table_rows) {
            const auto table=std::dynamic_pointer_cast<TableView>(w);
            if (!table || *entry.table_columns!=table->columns || *entry.table_rows!=table->rows ||
                entry.index_selection!=table->selection || entry.index_anchor!=table->selection_anchor ||
                entry.index_cursor!=table->selection_cursor || entry.top_index!=table->top_index ||
                entry.horizontal_offset!=table->horizontal_offset ||
                entry.row_height!=table->row_height || entry.header_height!=table->header_height ||
                entry.highlighted_column!=table->highlighted_column || entry.inline_edit_column!=table->inline_edit_column ||
                entry.multi_select!=table->multi_select || entry.checkable!=table->checkable ||
                entry.full_row_selection!=table->full_row_selection || entry.virtualized!=table->virtualized ||
                entry.check_selected_rows!=table->check_selected_rows ||
                entry.edit_on_double_click!=table->edit_on_double_click ||
                entry.header_visible!=table->header_visible || entry.grid_lines!=table->grid_lines ||
                entry.model_update_revision!=table->row_update_revision ||
                entry.model_order_revision!=table->row_order_revision || entry.scroll_revision!=table->scroll_update_revision)
                return false;
        }
        if (entry.tree_nodes) {
            const auto tree=std::dynamic_pointer_cast<TreeView>(w);
            if (!tree || *entry.tree_nodes!=tree->nodes || entry.tree_selection!=tree->selection ||
                entry.tree_anchor!=tree->selection_anchor || entry.tree_cursor!=tree->selection_cursor ||
                entry.top_index!=tree->top_index || entry.horizontal_offset!=tree->horizontal_offset ||
                entry.multi_select!=tree->multi_select || entry.checkable!=tree->checkable ||
                entry.show_expanders!=tree->show_expanders || entry.flush_root_left!=tree->flush_root_left ||
                entry.model_update_revision!=tree->node_update_revision ||
                entry.model_order_revision!=tree->node_order_revision || entry.scroll_revision!=tree->scroll_update_revision)
                return false;
        }
        if (entry.tab) {
            const auto tabs=std::dynamic_pointer_cast<TabView>(w);
            if (!tabs || entry.tab_page_update_revision!=tabs->page_update_revision ||
                entry.tab_page_order_revision!=tabs->page_order_revision) return false;
        }
    }
    return true;
}
SurfaceFramePresentation::SurfaceFramePresentation(std::vector<std::shared_ptr<Window>> windows)
    : windows_(std::move(windows)) {}
bool SurfaceFramePresentation::append(const std::shared_ptr<Window>& window, FrameStructureSnapshot snapshot) {
    if (frames_.size()>=windows_.size() || windows_[frames_.size()]!=window || !snapshot.current()) return false;
    frames_.push_back(std::move(snapshot));
    return true;
}
bool SurfaceFramePresentation::current(const std::vector<std::shared_ptr<Window>>& windows) const {
    return windows==windows_ && frames_.size()==windows_.size() &&
        std::all_of(windows_.begin(),windows_.end(),[](const auto& window) { return window && window->visible(); }) &&
        std::all_of(frames_.begin(),frames_.end(),[](const auto& frame) { return frame.current(); });
}
bool prepare_frame_drawing(WindowFramePresentation& frame,
    const DrawingCaptureServices& services,const std::function<bool()>& live) {
    const ::cwxeditor::lib::EditorPerformanceScope performance("frame.capture");
    const auto current=[&] { return (!live || live()) && frame.structure.current(); };
    if (!current()) return false;
    if (frame.drawing_prepared) return true;
    // アイコン、ImageView、Canvas のコマンドを通じて、各ID
    // は一度だけ解決する。 存在しないことも記録し、コールバックでフレーム途
    // 中から現れないようにする。 準備成功まではすべてローカルで保持する。
    std::unordered_map<ResourceId,std::shared_ptr<const ImageData>> resources;
    const auto retain=[&](ResourceId id)->std::shared_ptr<const ImageData> {
        if (!id) return {};
        if (const auto found=resources.find(id);found!=resources.end()) return found->second;
        if (!services.retain_image) throw std::logic_error("Frame image requires an image retention provider");
        auto image=services.retain_image(id);
        resources.emplace(id,image);
        return image;
    };
    const auto retain_icon=[&](ResourceId id) {
        if (!id || resources.contains(id)) return true;
        (void)retain(id);
        return current();
    };
    if (frame.shell && frame.shell->caption) {
        // 見出しの取り消し・やり直しのアイコンも、フレームの
        // 画像として保持する。 タイトルのアイコンだけでは
        // 、履歴アイコンの領域はあっても画像が描かれない。
        const auto& caption=*frame.shell->caption;
        if (!retain_icon(caption.icon) || !retain_icon(caption.undo_icon) ||
            !retain_icon(caption.redo_icon)) return false;
    }
    for (const auto& [owner,view]:frame.chrome)
        if (!retain_icon(view.icon)) return false;
    for (const auto& [owner,view]:frame.strips)
        for (const auto& item:view.items)
            if (!retain_icon(item.icon)) return false;
    for (const auto& [owner,view]:frame.item_views)
        for (const auto& row:view.rows)
            if (!retain_icon(row.warning_icon) || !retain_icon(row.comment_icon)) return false;
    for (const auto& [owner,view]:frame.item_views)
        for (const auto& row:view.rows)
            for (const auto& cell:row.cells)
                if (!retain_icon(cell.icon) || !retain_icon(cell.trailing_icon)) return false;
    for (const auto& [owner,view]:frame.tabs)
        for (const auto& header:view.headers)
            if (!retain_icon(header.icon)) return false;
    for (const auto& [owner,view]:frame.combo_controls)
        if (!retain_icon(view.icon)) return false;
    if (frame.combo)
        for (const auto& row:frame.combo->popup.rows)
            if (!retain_icon(row.icon)) return false;
    for (const auto& menu:frame.menus)
        for (const auto& row:menu.rows)
            if (!retain_icon(row.icon)) return false;
    // アプリの描画コールバックが画像の更新・破棄や配置
    // 属性の変更を行う前に、 既知の全世代を保持する。
    std::unordered_map<const ImageView*,ImagePresentation> images;
    std::unordered_map<const ImageView*,std::vector<ImagePresentation>> layers;
    for (const auto& [view,state]:frame.images) {
        if(state.layers.empty())
            images.emplace(view,prepare_image_presentation(retain(state.resource),
                state.requested_frame,state.viewport.content,state.scale_mode,state.interpolation));
        else {
            auto& drawings=layers[view]; drawings.reserve(state.layers.size());
            for(const auto& layer:state.layers) {
                drawings.push_back(prepare_image_layer_presentation(retain(layer.image),layer,state.viewport.content));
                if(!current()) return false;
            }
        }
        if (!current()) return false;
    }
    std::vector<DrawingCommandList> recordings;
    recordings.reserve(frame.canvases.size());
    auto capture=services;
    capture.retain_image=retain;
    for (const auto& canvas:frame.canvases) {
        if (!current()) return false;
        if (canvas.paint) recordings.push_back(record_drawing(canvas.paint,
            {0,0,canvas.bounds.width,canvas.bounds.height},capture));
        else {
            recordings.emplace_back();
            if (canvas.legacy_paint) canvas.legacy_paint(canvas.bounds);
        }
        if (!current()) return false;
    }
    for (std::size_t i=0;i<frame.canvases.size();++i) {
        frame.canvases[i].drawing=std::move(recordings[i]);
        frame.canvases[i].paint={}; frame.canvases[i].legacy_paint={};
    }
    for (auto& [owner,paint]:frame.tab_empty_client_paints) {
        (void)owner;
        if (!current()) return false;
        paint.drawing=record_drawing(paint.paint,
            {0,0,paint.bounds.width,paint.bounds.height},capture);
        paint.paint={}; paint.legacy_paint={};
        if (!current()) return false;
    }
    for (auto& [view,drawing]:images) frame.images.at(view).drawing=std::move(drawing);
    for (auto& [view,drawing]:layers) frame.images.at(view).layer_drawing=std::move(drawing);
    frame.resources=std::move(resources);
    frame.drawing_prepared=true;
    return true;
}
namespace {
std::string item_warning_tooltip(const WidgetPtr& view,Point pointer) {
    const auto index=hit_test_item_view(view,pointer);
    if (!index) return {};
    const std::vector<std::string>* warnings=nullptr;
    if (const auto list=std::dynamic_pointer_cast<ListView>(view)) {
        if (*index<list->items.size()) warnings=&list->items[*index].warnings;
    } else if (const auto table=std::dynamic_pointer_cast<TableView>(view)) {
        if (*index<table->rows.size()) warnings=&table->rows[*index].warnings;
    } else if (const auto tree=std::dynamic_pointer_cast<TreeView>(view)) {
        if (*index<tree->nodes.size()) warnings=&tree->nodes[*index].warnings;
    }
    std::string result;
    if (!warnings) return result;
    for (std::size_t i=0;i<warnings->size();++i) {
        if (i) result.push_back('\n');
        result+=(*warnings)[i];
    }
    return result;
}
std::optional<WindowFramePresentation> prepare_window_frame_impl(
    EditInputContext context, MenuPopupStateMachine& menus,
    std::shared_ptr<Window> window, Rect content, const std::function<bool()>& live, std::uint64_t now_ms,
    const WidgetPtr& hovered, Point pointer,const std::vector<PointerPressFeedback>& presses,
    TooltipStateMachine* tooltip_state) {
    const ::cwxeditor::lib::EditorPerformanceScope performance("frame.prepare");
    // フレームとIMEの表示位置は、同じ共通のメニューモードを使う
    // 。 単独の編集状況で表示側がメニューを渡さない場合も同様。
    context.menu = &menus;
    auto& editing=context.editing;
    WindowFramePresentation result;
    const auto current=[&] { return window && window->visible() && (!live || live()); };
    if (!current()) return {};
    window->bounds_ui_text_scale = window->kind() == ComponentKind::Dialog
        ? editor_ui_text_scale() : 1.0F;
    window->set_bounds(content);
    // 配置処理と、このフレームのスクロール形状・項目・文字の表示で、一つの
    // 計測メモを共有する。 詳細は ui_metrics.hpp を参照。
    const MeasurementCacheScope measurement_cache;
    if (!arrange_container(window,content) || !current()) return {};
    reconcile_item_view_scroll(window,context.row_height);
    if (!current()) return {};
    // 表示・提供処理より前に取得する。 最後に取得すると、古いツリー
    // から準備した描画を、新しいツリーに対して有効と認めてしまう。
    result.structure.capture(window);
    std::unordered_map<const Widget*,Rect> visible_bounds;
    std::vector<WidgetPtr> item_views;
    std::vector<WidgetPtr> strips;
    const auto prepare_text=[&](const std::shared_ptr<TextEdit>& edit) {
        if (!edit || !edit->visible()) return;
        result.styles.insert_or_assign(edit.get(),prepare_widget_style(*edit));
        const auto metrics=context.metrics(edit);
        auto& state=edit_input_text_state(context,edit);
        const auto inner=text_edit_inner_bounds(*edit);
        auto composition=project_text_composition(*edit,state);
        const auto& display=composition ? composition->edit : *edit;
        auto& display_state=composition ? composition->state : state;
        auto lines=layout_text_lines(display,inner.width,metrics.measure);
        clamp_text_edit_scroll(display,display_state,lines,inner,metrics.line_height);
        state.scroll_x=display_state.scroll_x; state.scroll_y=display_state.scroll_y;
        TextFramePresentation text{inner,metrics.line_height,std::move(lines),display_state};
        text.bounds=edit->bounds();
        text.scroll=text_edit_scroll_geometry(display,text.lines,metrics.line_height,display_state);
        text.scroll_decoration=prepare_scroll_decoration(text.scroll,edit.get());
        text.foreground=result.styles.at(edit.get()).foreground;
        const auto selection_start=std::min(display_state.anchor,display_state.caret);
        const auto selection_end=std::max(display_state.anchor,display_state.caret);
        for (std::size_t i=0;i<text.lines.size();++i) {
            const auto& line=text.lines[i];
            const auto origin=text_visual_line_origin(display,line,i,inner,metrics.line_height,display_state);
            if (origin.y+metrics.line_height<inner.y || origin.y>inner.y+inner.height) continue;
            TextLinePresentation draw{origin,text_display_range(display,line.start,line.end),{}};
            const auto first=std::max(selection_start,line.start);
            const auto last=std::min(selection_end,line.end);
            if (first<last) {
                const float before=measure_text_range(display,line.start,first,metrics.measure);
                const float width=measure_text_range(display,first,last,metrics.measure);
                draw.selection=Rect{origin.x+before,origin.y,width,metrics.line_height};
            }
            if (composition) {
                const auto composing_start=std::max(composition->range.start,line.start);
                const auto composing_end=std::min(composition->range.end(),line.end);
                if (composing_start<composing_end) {
                    const auto before=measure_text_range(display,line.start,composing_start,metrics.measure);
                    const auto width=measure_text_range(display,composing_start,composing_end,metrics.measure);
                    const auto thickness=scaled_ui_metric(1);
                    draw.composition_underline=Rect{origin.x+before,origin.y+metrics.line_height-thickness,width,thickness};
                }
            }
            text.visible_lines.push_back(std::move(draw));
        }
        result.text.insert_or_assign(edit.get(),std::move(text));
    };
    std::unordered_set<const Widget*> visited;
    const auto visit=[&](auto&& self,const WidgetPtr& widget,Rect clip)->void {
        if (!current() || !widget || !widget->visible() || !visited.insert(widget.get()).second) return;
        if (widget!=window && std::dynamic_pointer_cast<Window>(widget)) return;
        clip=intersect_rectangles(clip,widget->bounds());
        visible_bounds.emplace(widget.get(),clip);
        if (widget->kind()==ComponentKind::MenuBar || widget->kind()==ComponentKind::ToolBar)
            strips.push_back(widget);
        result.styles.insert_or_assign(widget.get(),prepare_widget_style(*widget));
        const bool interactive_chrome=dynamic_cast<const Button*>(widget.get()) || widget->kind()==ComponentKind::Link;
        const bool pointer_feedback=interactive_chrome && !menus.is_active() && !editing.combo_popup.combo() &&
            widget_in_input_scope(window,widget);
        const bool pressed=pointer_feedback && std::any_of(presses.begin(),presses.end(),[&](const auto& press) {
            return press.widget==widget && point_inside(clip,press.position) && point_inside(widget->bounds(),press.origin);
        });
        if (auto chrome=prepare_widget_chrome(*widget,{pointer_feedback && hovered==widget && point_inside(clip,pointer),
                pressed,editing.focus.focused()==widget && widget_in_input_scope(window,widget)})) {
            result.decorations.emplace(widget.get(),prepare_chrome_decoration(*chrome,widget->bounds()));
            result.chrome.emplace(widget.get(),std::move(*chrome));
        }
        const auto canvas = std::dynamic_pointer_cast<Canvas>(widget);
        if (canvas || widget->on_paint_context) {
            CanvasFramePresentation view{widget,widget->bounds(),widget->on_paint_context,
                canvas ? canvas->on_paint : std::function<void(Rect)>{},{}};
            if (canvas && canvas->border)
                view.decoration.push_back({DecorationKind::StrokeRect,{145,145,145,255},canvas->bounds(),{}});
            result.canvases.push_back(std::move(view));
        }
        if (const auto image=std::dynamic_pointer_cast<ImageView>(widget)) {
            const auto viewport=prepare_image_viewport(image->bounds(),image->border);
            result.images.emplace(image.get(),ImageViewFramePresentation{image->image,image->frame,
                image->bounds(),image->scale_mode,image->interpolation,{},
                viewport,prepare_image_frame_decoration(viewport.outer,viewport.recessed_border),image->layers,{}});
        }
        if(nonempty(clip)) {
            if(auto decoration=prepare_value_decoration(*widget,now_ms,
                editing.focus.focused()==widget && widget_in_input_scope(window,widget)))
                result.decorations.emplace(widget.get(),std::move(*decoration));
            if(const auto progress=std::dynamic_pointer_cast<ProgressBar>(widget); progress && progress->indeterminate)
                result.animation.request_tick(now_ms,16);
        }
        if (const auto text=std::dynamic_pointer_cast<TextEdit>(widget)) prepare_text(text);
        if (const auto number=std::dynamic_pointer_cast<NumberEdit>(widget))
            result.numbers.emplace(number.get(),NumberFramePresentation{
                layout_number_edit(*number),format_number_value(*number),
                number->enabled() && !number->read_only && number->value<number->maximum,
                number->enabled() && !number->read_only && number->value>number->minimum,{}});
        if (const auto combo=std::dynamic_pointer_cast<ComboBox>(widget)) {
            const auto geometry=layout_combo_box(*combo);
            ResourceId icon{};
            if(combo->selected_index && *combo->selected_index<combo->items.size())
                icon=combo->items[*combo->selected_index].icon;
            result.combo_controls.emplace(combo.get(),ComboControlPresentation{
                geometry,combo_display_text(*combo),icon,{},{}});
            auto& view = result.combo_controls.at(combo.get());
            view.editable = combo->editable;
            view.enabled = combo->enabled();
            view.empty_hint = combo->empty_hint;
            if (view.empty_hint.empty() && view.editable && view.enabled)
                view.empty_hint = "入力または候補を選択";
        }
        if (auto* list=dynamic_cast<ListView*>(widget.get())) {
            item_views.push_back(widget);
            const auto scroll=item_scroll_geometry(*list,context.row_height);
            (void)set_item_view_top_index(*list,list->top_index,scroll.visible_rows);
        } else if (auto* table=dynamic_cast<TableView*>(widget.get())) {
            item_views.push_back(widget);
            (void)set_table_horizontal_offset(*table,table->horizontal_offset);
            const auto scroll=item_scroll_geometry(*table,context.row_height);
            (void)set_item_view_top_index(*table,table->top_index,scroll.visible_rows);
        } else if (auto* tree=dynamic_cast<TreeView*>(widget.get())) {
            item_views.push_back(widget);
            (void)set_item_horizontal_offset(*tree,tree->horizontal_offset);
            (void)set_item_view_top_index(*tree,tree->top_index,tree_visible_row_count(*tree,context.row_height));
        }
        if (const auto container=std::dynamic_pointer_cast<Container>(widget)) {
            auto view=prepare_container_presentation(*container);
            clip=intersect_rectangles(clip,view.viewport);
            result.containers.emplace(container.get(),std::move(view));
        }
        if (const auto tabs=std::dynamic_pointer_cast<TabView>(widget)) {
            const auto snapshot=snapshot_tab_layout(*tabs);
            auto view=prepare_tab_view(*tabs,tabs==hovered,pointer);
            if (!current() || !tab_layout_is_current(snapshot,*tabs)) return;
            if (tabs->pages.empty() && tabs->on_empty_client_paint) {
                result.tab_empty_client_paints.emplace(tabs.get(),
                    CanvasFramePresentation{tabs,view.content,tabs->on_empty_client_paint,{},{}});
            }
            const auto page=view.selected_page;
            const auto page_clip=intersect_rectangles(clip,view.content);
            result.tabs.emplace(tabs.get(),std::move(view));
            // 選択ページはタブ見出しの領域には表示しない。 準備時のクリッ
            // プを親の範囲のままにすると、 大きなSceneViewのキ
            // ャンバスが毎フレーム見出しの背後の画素まで準備してしまう。
            self(self,page,page_clip);
            return;
        }
        for (const auto& child:frame_children(widget)) self(self,child,clip);
    };
    visit(visit,window,content);
    if (!current() || !result.structure.current()) return {};
    auto& inline_edit=editing.inline_edit;
    if (inline_edit.active() && (!inline_edit.owner() || contains_widget(window,inline_edit.owner().get()))) {
        if (!inline_edit.owner() || !widget_in_input_scope(window,inline_edit.owner()) ||
            !inline_edit.layout(context.row_height)) {
            if (editing.combo_popup.is_open_for(inline_edit.choice())) editing.combo_popup.close();
            inline_edit.cancel();
        } else {
            result.inline_editor=true;
            result.inline_owner=inline_edit.owner().get();
            result.inline_text=inline_edit.editor();
            if (const auto choice=inline_edit.choice()) result.inline_arrow=layout_combo_box(*choice).arrow;
            if (const auto stepper = prepare_inline_number_stepper(inline_edit, context.row_height)) {
                NumberFramePresentation view;
                view.geometry = stepper->geometry;
                view.increase_enabled = stepper->increase_enabled;
                view.decrease_enabled = stepper->decrease_enabled;
                view.editor = result.inline_text;
                view.decoration = prepare_number_decoration(view.geometry,
                    view.increase_enabled, view.decrease_enabled, {24,24,24,255});
                result.inline_number = std::move(view);
            }
            prepare_text(result.inline_text);
            result.text.at(result.inline_text.get()).foreground={24,24,24,255};
            if(result.inline_arrow) {
                result.inline_decoration={{DecorationKind::FillRect,{240,240,240,255},*result.inline_arrow,{}},
                                          {DecorationKind::StrokeRect,{145,145,145,255},*result.inline_arrow,{}}};
                result.inline_label=DecorationTextPresentation{"▼",*result.inline_arrow,TextAlignment::Center,{24,24,24,255},0};
            }
        }
    }
    const auto focused=editing.focus.focused();
    if (focused && widget_in_input_scope(window,focused)) {
        if (const auto number=std::dynamic_pointer_cast<NumberEdit>(focused)) {
            // 古い編集状態の失効は、フォーカス遷移ではない。 新しい論理
            // 所有者のフレーム準備の副作用として、その編集を確定しない。
            editing.number_edit.project(number);
            prepare_text(editing.number_edit.editor());
            result.text.at(editing.number_edit.editor().get()).foreground=result.styles.at(number.get()).foreground;
            if (const auto entry=result.numbers.find(number.get()); entry!=result.numbers.end())
                entry->second.editor=editing.number_edit.editor();
        } else if (const auto combo=std::dynamic_pointer_cast<ComboBox>(focused); combo && combo->editable) {
            editing.combo_edit.begin(combo);
            prepare_text(editing.combo_edit.editor());
            result.text.at(editing.combo_edit.editor().get()).foreground=result.styles.at(combo.get()).foreground;
            if (const auto entry=result.combo_controls.find(combo.get()); entry!=result.combo_controls.end())
                entry->second.editor=editing.combo_edit.editor();
        }
    }
    std::string combo_popup_help;
    if (const auto combo=editing.combo_popup.combo(); combo &&
        (contains_widget(window,combo.get()) || (combo==inline_edit.choice() &&
         contains_widget(window,inline_edit.owner().get())))) {
        if (!combo_in_scope(window,combo,inline_edit)) editing.combo_popup.close();
        else {
            const auto geometry=editing.combo_popup.layout(content);
            result.styles.insert_or_assign(combo.get(),prepare_widget_style(*combo));
            result.combo=ComboFramePresentation{combo,prepare_combo_presentation(*combo,geometry,
                editing.combo_popup.top(),editing.combo_popup.highlighted())};
            result.combo->popup.scroll_thumb_pressed=editing.combo_popup.dragging_thumb();
            if(const auto row=geometry.item_at(pointer,editing.combo_popup.top(),combo->items.size());
               row && *row<combo->items.size())
                combo_popup_help=combo->items[*row].tooltip;
        }
    }
    if (menus.is_active() && contains_widget(window,menus.owner().get())) {
        if (!widget_in_input_scope(window,menus.owner())) menus.close();
        else {
            const auto geometry=menus.layout(content);
            const auto& levels=menus.levels();
            for (std::size_t i=0;i<geometry.size() && i<levels.size();++i)
                result.menus.push_back(prepare_menu_presentation(*levels[i].menu,geometry[i],levels[i].selected));
        }
    }
    if (!current()) return {};
    result.focus_revision=editing.focus.revision();
    result.text_input=prepare_text_input(context,window);
    if (!current()) return {};
    // 通常の文字、数値、編集可能コンボ、インライン編集で、同じ準備済み
    // テキストと論理フォーカスからカーソルを求める。 読み取り専用に
    // も選択カーソルはあるが、ネイティブIMEの表示位置は持たない。
    std::shared_ptr<TextEdit> caret_edit;
    if (focused && widget_in_input_scope(window,focused)) {
        if (result.inline_editor && inline_edit.owner()==focused) caret_edit=inline_edit.editor();
        else if (const auto text=std::dynamic_pointer_cast<TextEdit>(focused)) caret_edit=text;
        else if (std::dynamic_pointer_cast<NumberEdit>(focused)) caret_edit=editing.number_edit.editor();
        else if (const auto combo=std::dynamic_pointer_cast<ComboBox>(focused); combo && combo->editable)
            caret_edit=editing.combo_edit.editor();
    }
    if (caret_edit) {
        const auto prepared=result.text.find(caret_edit.get());
        const auto visible=visible_bounds.find(focused.get());
        if (prepared!=result.text.end()) {
            prepared->second.focused=true;
            const auto popup=editing.combo_popup.combo();
            if (!menus.is_active() && !popup && visible!=visible_bounds.end() && nonempty(visible->second)) {
                const auto metrics=context.metrics(caret_edit);
                if (!current() || editing.focus.revision()!=result.focus_revision) return {};
                const auto& text=prepared->second;
                const auto composition=project_text_composition(*caret_edit,edit_input_text_state(context,caret_edit));
                const auto caret=text_edit_caret_bounds(composition ? composition->edit : *caret_edit,text.lines,text.inner,
                    text.line_height,text.state,metrics.measure);
                if (nonempty(intersect_rectangles(caret,visible->second))) {
                    result.animation.request_tick(now_ms,500);
                    if ((now_ms/500)%2==0) prepared->second.caret=caret;
                }
            }
        }
    }
    for(auto& [owner,view]:result.text) {
        const bool number_editor = owner == editing.number_edit.editor().get();
        if (number_editor) {
            // D版との意図的な違いとして、編集中の数値は黒字と
            // 淡い選択色にする。 D版の濃い青のネイティブ選択
            // 色では小さい数字が読みにくいため。 通常のテキ
            // スト編集は、OS形式の青い選択表示を維持する。
            view.selection_foreground = {24,24,24,255};
        }
        const auto style=result.styles.find(owner);
        const auto background=style!=result.styles.end() && style->second.background
            ? *style->second.background : Color{255,255,255,255};
        view.decoration={{DecorationKind::FillRect,background,view.bounds,{}},
                         {DecorationKind::StrokeRect,view.focused?Color{49,106,197,255}:Color{145,145,145,255},view.bounds,{}}};
        for(auto& line:view.visible_lines) {
            if(line.selection) line.decoration.push_back({DecorationKind::FillRect,
                number_editor ? Color{205,222,245,255} : Color{49,106,197,255},
                *line.selection,{}});
            if(line.composition_underline) line.decoration.push_back({DecorationKind::FillRect,view.foreground,*line.composition_underline,{}});
        }
        if(view.caret) view.caret_decoration.push_back({DecorationKind::FillRect,view.foreground,*view.caret,{}});
    }
    // 値の操作部品の外観は、共通のフォーカス・編集表示を作った後で取
    // 得する。 描画アダプターはフレーム再生中に現在のウィジェットを
    // 参照し、 文字余白・矢印・色・有効状態の装飾を選び直さない。
    for(auto& [owner,view]:result.numbers) {
        const auto foreground=result.styles.at(owner).foreground;
        if(!view.editor) {
            view.decoration.push_back({DecorationKind::FillRect,{255,255,255,255},view.geometry.text,{}});
            view.decoration.push_back({DecorationKind::StrokeRect,{145,145,145,255},view.geometry.text,{}});
            view.labels.push_back({view.text,view.geometry.text,TextAlignment::End,foreground,
                owner->zero_text_padding ? 0.0F : scaled_ui_metric(4)});
        }
        if(!owner->inline_label.empty() && view.geometry.label.width>0)
            view.labels.push_back({owner->inline_label,view.geometry.label,TextAlignment::Center,foreground,0.0F});
        auto arrows=prepare_number_decoration(view.geometry,view.increase_enabled,view.decrease_enabled,foreground);
        view.decoration.insert(view.decoration.end(),std::make_move_iterator(arrows.begin()),std::make_move_iterator(arrows.end()));
    }
    for(auto& [owner,view]:result.combo_controls) {
        const auto& style=result.styles.at(owner);
        view.decoration.push_back({DecorationKind::FillRect,
            view.enabled ? Color{255,255,255,255} : Color{238,238,238,255},style.bounds,{}});
        if (view.editable) {
            // 左を自由入力、右を選択肢として、別々の二対象を公開
            // する。 共通の当たり判定とフォント寸法は変えない。
            view.decoration.push_back({DecorationKind::FillRect,{238,238,238,255},view.geometry.arrow,{}});
            view.decoration.push_back({DecorationKind::FillRect,{145,145,145,255},
                {view.geometry.arrow.x,style.bounds.y,scaled_ui_metric(1),style.bounds.height},{}});
        }
        view.decoration.push_back({DecorationKind::StrokeRect,{145,145,145,255},style.bounds,{}});
        auto text_bounds=view.geometry.text;
        if(view.icon) {
            const float size=scaled_ui_metric(20),gap=scaled_ui_metric(4);
            view.icon_bounds={text_bounds.x+scaled_ui_metric(4),
                text_bounds.y+std::max(0.0F,(text_bounds.height-size)*.5F),size,size};
            const float consumed=scaled_ui_metric(4)+size+gap;
            text_bounds.x+=consumed;
            text_bounds.width=std::max(0.0F,text_bounds.width-consumed);
        }
        if(!view.editor) {
            const bool hint = view.text.empty() && !view.empty_hint.empty();
            view.labels.push_back({hint ? view.empty_hint : view.text,text_bounds,TextAlignment::Start,
                hint ? Color{130,130,130,255} : style.foreground,scaled_ui_metric(5)});
        }
        view.labels.push_back({"▼",view.geometry.arrow,TextAlignment::Center,style.foreground,1.0F});
    }
    result.window=window;
    for (const auto& view:item_views)
        result.item_views.emplace(view.get(),prepare_item_view(view,context.row_height,
            &inline_edit,view==focused && widget_in_input_scope(window,view),
            view==hovered ? hit_test_item_view(view,pointer) : std::nullopt));
    for (const auto& widget:strips) {
        if (const auto menu=std::dynamic_pointer_cast<Menu>(widget))
            result.strips.emplace(widget.get(),prepare_menu_bar(*menu,
                menus.selected_menu_bar_item(*menu),widget==hovered,pointer));
        else if (const auto toolbar=std::dynamic_pointer_cast<ToolBar>(widget))
            result.strips.emplace(widget.get(),prepare_tool_bar(
                *toolbar,widget==hovered,pointer,presses,widget==focused));
    }
    bool tooltip_candidate=false;
    if (hovered && !menus.is_active() && !editing.combo_popup.combo() && presses.empty()) {
        const auto visible=visible_bounds.find(hovered.get());
        if (visible!=visible_bounds.end() && point_inside(visible->second,pointer)) {
            const auto toolbar=std::dynamic_pointer_cast<ToolBar>(hovered);
            std::string comment_help;
            std::optional<std::size_t> toolbar_help_item;
            std::string toolbar_title,toolbar_body;
            bool toolbar_description=false;
            if (toolbar && toolbar->tooltip_width_provider) {
                toolbar_help_item=hit_test_tool_bar_item(*toolbar,pointer);
                if (toolbar_help_item && *toolbar_help_item<toolbar->items.size()) {
                    const auto& item=toolbar->items[*toolbar_help_item];
                    const auto separator=item.tooltip.find('\n');
                    if (separator!=std::string::npos && separator>0) {
                        toolbar_title=item.tooltip.substr(0,separator);
                        toolbar_body=item.tooltip.substr(separator+1);
                        toolbar_description=true;
                    }
                }
            }
            if (const auto item_view=result.item_views.find(hovered.get());
                item_view!=result.item_views.end())
                comment_help=item_comment_tooltip(hovered,pointer,item_view->second);
            const auto row_warning=item_warning_tooltip(hovered,pointer);
            const auto help=!comment_help.empty() ? comment_help
                : !row_warning.empty() ? row_warning
                : toolbar ? tool_bar_tooltip(*toolbar,pointer) : hovered->tooltip();
            if (toolbar_description && toolbar_help_item) {
                toolbar_description=comment_help.empty() && row_warning.empty() &&
                    help==toolbar->items[*toolbar_help_item].tooltip;
            }
            if (!help.empty()) {
                tooltip_candidate=true;
                const auto tooltip=std::dynamic_pointer_cast<ToolTip>(hovered);
                const bool balloon=hovered->tooltip_balloon() || (tooltip && tooltip->balloon);
                TooltipTimingResult timing{true,pointer,{}};
                if (tooltip_state) {
                    TooltipTiming options; options.movement_tolerance=scaled_ui_metric(4);
                    if (toolbar_description) options.immediate_override=true;
                    timing=tooltip_state->update(window,hovered,
                        toolbar ? hit_test_tool_bar_item(*toolbar,pointer) : hit_test_item_view(hovered,pointer),
                        help,pointer,balloon,now_ms,options);
                    if (timing.next_update) result.animation.request_at(*timing.next_update);
                }
                // 案内には Canvas・カード用ではなく、標準のUIフォントを使う。
                if (timing.visible) {
                    const auto metrics=context.metrics(std::make_shared<Label>());
                    if (!current()) return {};
                    const auto width_provider=toolbar ? toolbar->tooltip_width_provider : std::function<float()>{};
                    const auto width=width_provider ? width_provider() : 0.0F;
                    if (!current()) return {};
                    if (toolbar_description && toolbar_help_item &&
                        *toolbar_help_item<toolbar->items.size()) {
                        const auto geometry=tool_bar_item_geometry(*toolbar);
                        if (*toolbar_help_item<geometry.size())
                            result.tooltip=prepare_toolbar_description_tooltip(toolbar_title,toolbar_body,
                                toolbar->items[*toolbar_help_item].icon,geometry[*toolbar_help_item].bounds,
                                content,metrics.line_height,metrics.measure,width);
                    } else {
                        result.tooltip=prepare_tooltip(help,timing.anchor,content,metrics.line_height,metrics.measure,balloon,width);
                    }
                }
            }
        }
    }
    if (!tooltip_candidate && tooltip_state) {
        // 押し続けている接触は、抑止中の状態を残したまま案内を隠す。 同
        // じ位置でボタンを離しただけで、再び案内が開かないようにする。
        if (!presses.empty()) tooltip_state->dismiss_for(window);
        else tooltip_state->cancel_for(window);
    }
    // 字形寸法と取得済みの実効フォントがあるうちに、一行ラベルを
    // 確定する。 Canvas のコールバックはその後にだけ呼び
    // 、 アダプターはフレーム再生中にラベル位置を再計算しない。
    const auto default_font=effective_editor_widget_font();
    const auto font_for=[&](const Widget* owner) {
        const auto style=result.styles.find(owner);
        return style!=result.styles.end() ? style->second.font.value_or(default_font) : default_font;
    };
    const auto label=[&](DecorationTextPresentation& value,const FontSpec& font) {
        value.text_layout=prepare_aligned_text(value.text,value.bounds,value.alignment,value.padding,font);
    };
    for(auto& [owner,value]:result.chrome) {
        if(value.multiline)
            value.paragraph=prepare_label_text(value.text,value.text_bounds,value.alignment,
                value.text_padding,value.wrap,font_for(owner));
        else if(!value.title_origin)
            value.text_layout=prepare_aligned_text(value.text,value.text_bounds,value.alignment,value.text_padding,font_for(owner));
    }
    for(auto& [owner,value]:result.numbers)
        for(auto& text:value.labels) label(text,font_for(owner));
    for(auto& [owner,value]:result.combo_controls)
        for(auto& text:value.labels) label(text,font_for(owner));
    if(result.inline_label) label(*result.inline_label,font_for(result.inline_text.get()));
    for(auto& [owner,value]:result.item_views) {
        const auto font=font_for(owner);
        for(auto& header:value.headers)
            header.text_layout=prepare_aligned_text(header.text,header.bounds,header.alignment,header.text_padding,font);
        for(auto& row:value.rows) {
            if(row.expander_label) label(*row.expander_label,font);
            if(row.warning_label) label(*row.warning_label,font);
            for(auto& cell:row.cells)
                if(!cell.inline_editor)
                    cell.text_layout=prepare_aligned_text(cell.text,cell.text_bounds,cell.alignment,cell.text_padding,font);
        }
    }
    for(auto& [owner,value]:result.tabs) {
        const auto font=font_for(owner);
        for(auto& header:value.headers)
            for(auto& text:header.labels) label(text,font);
        for(auto& navigation:value.navigation) label(navigation.label,font);
    }
    for(auto& [owner,value]:result.containers)
        for(auto& section:value.sections)
            for(auto& text:section.labels) label(text,section.style.font.value_or(default_font));
    for(auto& [owner,value]:result.strips) {
        const auto font=font_for(owner);
        for(auto& item:value.items) {
            if(!item.separator && !item.top_aligned && !item.text.empty())
                item.text_layout=prepare_aligned_text(item.text,item.text_bounds,item.text_alignment,item.text_padding,font);
            if(item.arrow) label(*item.arrow,font);
        }
    }
    const auto popup=[&](PopupPresentation& value) {
        const auto font=value.font.value_or(default_font);
        for(auto& row:value.rows)
            for(auto& text:row.text)
                text.text_layout=prepare_aligned_text(text.text,text.bounds,text.alignment,0,font);
    };
    if(result.combo) popup(result.combo->popup);
    for(auto& menu:result.menus) popup(menu);
    if(!combo_popup_help.empty()) {
        const auto metrics=context.metrics(std::make_shared<Label>());
        if(!current()) return {};
        result.tooltip=prepare_tooltip(combo_popup_help,pointer,content,
                                       metrics.line_height,metrics.measure,false);
    }
    if(!current() || !result.structure.current()) return {};
    return result;
}
} // namespace

std::optional<WindowFramePresentation> prepare_window_frame(
    EditInputContext context, MenuPopupStateMachine& menus,
    std::shared_ptr<Window> window, Rect content, const std::function<bool()>& live,
    const std::uint64_t now_ms, const WidgetPtr& hovered, const Point pointer,
    const std::vector<PointerPressFeedback>& presses, TooltipStateMachine* tooltip_state) {
    // 配置・寸法のスナップショットは、文字列・行・フレーム状態を所有して
    // 確保する。 確保失敗は未完了のフレームとして扱い、一部だけを反映し
    // ない。 呼び出し元は以前の完成ラスターを保ち、後で再試行できる。
    try {
        return prepare_window_frame_impl(std::move(context), menus, std::move(window), content,
                                         live, now_ms, hovered, pointer, presses, tooltip_state);
    } catch (const std::bad_alloc&) {
        return {};
    } catch (const std::length_error&) {
        return {};
    }
}
} // namespace cwxeditor::lib
