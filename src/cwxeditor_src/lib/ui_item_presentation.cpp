#include "ui_item_presentation.hpp"
#include "ui_metrics.hpp"
#include "window_host.hpp"
#include <algorithm>
#include <cmath>

namespace cwxeditor::lib {
namespace {
float metric(float value) { return scaled_ui_metric(value); }
template<class T> bool selected(const std::vector<T>& values,T value) {
    return std::find(values.begin(),values.end(),value)!=values.end();
}
void checkbox(ItemCellPresentation& cell,std::optional<Rect> bounds,bool checked,
              bool grayed=false) {
    cell.check_box=bounds;
    cell.check_grayed=grayed;
    if (checked && bounds) cell.check_mark=Rect{bounds->x+metric(3),bounds->y+metric(3),
        std::max(0.0F,bounds->width-metric(6)),std::max(0.0F,bounds->height-metric(6))};
}
void image(ItemCellPresentation& cell,ResourceId icon,float& x,float y,float row_height) {
    cell.icon=icon;
    if (!icon) return;
    const auto size=metric(16);
    cell.icon_bounds={x,y+(row_height-size)*.5F,size,size};
    // チェック付きの行は詰めて配置する。 アイコンとラベルの間
    // は一論理画素で十分とし、シーン一覧の利用可能な幅を保つ。
    x+=size+metric(1);
}
void warning(ItemRowPresentation& row,const std::vector<std::string>& warnings,
             ResourceId icon,Rect viewport,float y,float row_height,
             std::optional<float> right_edge = {}) {
    if (warnings.empty()) return;
    const auto size=metric(16);
    const float edge = right_edge.value_or(viewport.x+viewport.width);
    row.warning_bounds=Rect{edge-metric(5)-size,
        y+(row_height-size)*.5F,size,size};
    row.warning_icon=icon;
    for (std::size_t i=0;i<warnings.size();++i) {
        if (i) row.warning_text.push_back('\n');
        row.warning_text+=warnings[i];
    }
    if (!icon) row.warning_label=DecorationTextPresentation{
        "!",*row.warning_bounds,TextAlignment::Center,Color{196,43,28,255}};
}
void comment_marker(ItemRowPresentation& row, std::string_view text,
                    ResourceId icon, Rect viewport, float y,
                    float row_height) {
    if (text.empty() || !icon) return;
    const auto size=metric(16);
    row.comment_bounds=comment_marker_bounds(
        {viewport.x,y,viewport.width,row_height},{size,size},metric(5),
        CommentMarkerPlacement::CenterRight);
    row.comment_icon=icon;
    row.comment_text.assign(text);
}
}

ItemViewPresentation prepare_item_view(const WidgetPtr& view,float row_height,
    const InlineEditStateMachine* inline_edit,bool focused,
    std::optional<std::size_t> hovered_index) {
    ItemViewPresentation result;
    if (!view) return result;
    row_height=item_row_height(*view,row_height);
    result.bounds=view->bounds(); result.scroll=item_scroll_geometry(*view,row_height);
    const auto r=result.bounds, viewport=result.scroll.viewport;
    if (focused && (!inline_edit || !inline_edit->active()))
        result.cursor=item_view_cursor_geometry(view,row_height);
    if (const auto list=std::dynamic_pointer_cast<ListView>(view)) {
        // ネイティブの一覧・ツリーと同様、先頭行をクライア
        // ント原点から始める。 仮の2pxの見出しは、これ
        // らを使う全ダイアログに空の帯を作るため設けない。
        float y=viewport.y;
        for (std::size_t i=list->top_index;i<list->items.size() && y+row_height<=viewport.y+viewport.height;++i,y+=row_height) {
            const auto& item=list->items[i];
            ItemRowPresentation row; row.model_index=i;
            if (selected(list->selection,i)) row.selection=Rect{viewport.x+metric(2),y,
                std::max(0.0F,viewport.width-metric(4)),row_height};
            ItemCellPresentation cell; cell.text=item.text; cell.enabled=item.enabled;
            float x=viewport.x+metric(6);
            if (list->checkable) {
                checkbox(cell,linear_row_check_box_geometry(viewport,y,row_height,metric(6),metric(13)),item.checked);
                x+=metric(14);
            }
            image(cell,item.icon,x,y,row_height);
            warning(row,item.warnings,item.warning_icon,viewport,y,row_height);
            const auto warning_reserve=row.warning_bounds ? metric(21) : 0.0F;
            cell.text_bounds={x,y,std::max(0.0F,viewport.x+viewport.width-x-metric(5)-warning_reserve),row_height};
            row.cells.push_back(std::move(cell)); result.rows.push_back(std::move(row));
        }
    } else if (const auto table=std::dynamic_pointer_cast<TableView>(view)) {
        const float height=table_row_height(*table,row_height), header=table_header_height(*table,row_height);
        const auto widths=resolved_table_column_widths(*table,viewport.width);
        if (table->highlighted_column) result.highlight=table_body_column_geometry(*table,*table->highlighted_column,row_height);
        float x=r.x-table->horizontal_offset;
        for (std::size_t c=0;table->header_visible && c<table->columns.size();++c) {
            const auto& column=table->columns[c];
            result.headers.push_back({column.title,{x,r.y,widths[c],header},
                {x+widths[c]-1,r.y+metric(3),1,std::max(0.0F,header-metric(6))},column.alignment});
            x+=widths[c];
        }
        for (std::size_t i=table->top_index;i<table->rows.size();++i) {
            const float y=table_row_geometry(*table,i,row_height).y;
            if (y+height>viewport.y+viewport.height) break;
            ItemRowPresentation row; row.model_index=i;
            if (selected(table->selection,i)) row.selection=table_row_selection_geometry(*table,y,height);
            x=r.x-table->horizontal_offset;
            for (std::size_t c=0;c<table->columns.size();++c) {
                ItemCellPresentation cell;
                if (table->grid_lines) cell.grid=Rect{x,y,widths[c],height};
                cell.alignment=table->columns[c].alignment;
                float text_x=x+metric(5);
                // チェック状態は任意の先頭セルではなく行に属す
                // る。 文字セルの再構築中で行が不完全でも、DWT
                // ・SWTの灰色のチェック状態を維持する。
                if (table->checkable && c==0) {
                    checkbox(cell,linear_row_check_box_geometry({r.x-table->horizontal_offset,r.y,r.width,r.height},
                        y,height,metric(5),metric(13)),table->rows[i].checked,
                        table->rows[i].grayed);
                    text_x+=metric(14);
                }
                if (c<table->rows[i].cells.size()) {
                    const auto& source=table->rows[i].cells[c];
                    cell.text=source.text; cell.enabled=source.enabled;
                    cell.text_padding=2;
                    image(cell,source.icon,text_x,y,height);
                    cell.trailing_icon=source.trailing_icon;
                    const auto trailing_size=source.trailing_icon ? metric(16) : 0.0F;
                    if (source.trailing_icon)
                        cell.trailing_icon_bounds={x+widths[c]-metric(5)-trailing_size,
                            y+(height-trailing_size)*.5F,trailing_size,trailing_size};
                    cell.inline_editor=inline_edit && inline_edit->matches(table,i,c);
                }
                const auto trailing_reserve=cell.trailing_icon ? metric(21) : 0.0F;
                cell.text_bounds={text_x,y,std::max(0.0F,widths[c]-(text_x-x)-trailing_reserve),height};
                row.cells.push_back(std::move(cell)); x+=widths[c];
            }
            // D版の comment.d と同様、カード表の印は表示領域の
            // 端に置き、 スクロール中も指定列幅の合計で位置を制限する。
            float column_width{};
            for (const auto width : widths) column_width += width;
            auto comment_viewport=viewport;
            comment_viewport.width=std::min(viewport.width,column_width);
            comment_marker(row,table->rows[i].comment_text,
                           table->rows[i].comment_icon,comment_viewport,y,height);
            warning(row,table->rows[i].warnings,table->rows[i].warning_icon,
                    viewport,y,height,row.comment_bounds
                        ? std::optional<float>{row.comment_bounds->x}
                        : std::nullopt);
            if (!row.cells.empty() && (row.comment_bounds || row.warning_bounds)) {
                // 表示領域の端が最後の指定列の途中に来る場合
                // がある。 表示領域外に幅を確保せず、最も左
                // の印より手前で、そのセルの文字を切り取る。
                auto& cell=row.cells.back();
                const float marker_x=row.warning_bounds ? row.warning_bounds->x
                                                        : row.comment_bounds->x;
                const float text_limit=marker_x-metric(5);
                cell.text_bounds.width=std::max(0.0F,
                    std::min(cell.text_bounds.x+cell.text_bounds.width,text_limit)-
                    cell.text_bounds.x);
            }
            result.rows.push_back(std::move(row));
        }
    } else if (const auto tree=std::dynamic_pointer_cast<TreeView>(view)) {
        const auto visible=visible_tree_indices(*tree);
        float y=r.y;
        for (std::size_t i=std::min(tree->top_index,visible.size());i<visible.size();++i) {
            // 単独版はD版と同様、一部だけ見える最後のツリー行とその警告も含め
            // る。 フレーム再生で表示範囲に切り取り、ALL は変更しない。
            if (editor_window_host_options().all_mode
                    ? y+row_height>viewport.y+viewport.height
                    : y>=viewport.y+viewport.height) break;
            const auto index=visible[i]; const auto& node=tree->nodes[index];
            const auto geometry=tree_row_geometry(*tree,viewport,index,y,row_height);
            if (!geometry) continue;
            ItemRowPresentation row; row.model_index=index; row.item_id=node.id; row.expanded=node.expanded;
            if (selected(tree->selection,node.id)) row.selection=Rect{r.x+metric(2),y,
                std::max(0.0F,viewport.width-metric(4)),row_height};
            if (geometry->has_expander) row.expander=geometry->expander;
            ItemCellPresentation cell; cell.text=node.text;
            if (tree->checkable) checkbox(cell,geometry->check_box,node.checked);
            cell.icon=node.icon; cell.icon_bounds=geometry->icon;
            comment_marker(row,node.comment_text,node.comment_icon,viewport,y,row_height);
            warning(row,node.warnings,node.warning_icon,viewport,y,row_height,
                    row.comment_bounds
                        ? std::optional<float>{row.comment_bounds->x}
                        : std::nullopt);
            if (row.comment_bounds) {
                cell.trailing_icon=row.comment_icon;
                cell.trailing_icon_bounds=*row.comment_bounds;
            }
            const auto warning_reserve=row.warning_bounds ? metric(21) : 0.0F;
            const auto comment_reserve=row.comment_bounds ? metric(21) : 0.0F;
            cell.text_bounds={geometry->text.x,y,std::max(0.0F,r.x+r.width-geometry->text.x-metric(5)-warning_reserve-comment_reserve),row_height};
            cell.inline_editor=inline_edit && inline_edit->matches(tree,index);
            row.cells.push_back(std::move(cell)); result.rows.push_back(std::move(row)); y+=row_height;
        }
    }
    const Color authored_foreground=view->foreground().value_or(Color{24,24,24,255});
    const Color authored_background=view->background().value_or(Color{255,255,255,255});
    result.decoration={{DecorationKind::FillRect,authored_background,r,{}},
        {DecorationKind::StrokeRect,{145,145,145,255},r,{}}};
    if(result.highlight) result.content_decoration.push_back(
        {DecorationKind::FillRect,{247,247,247,255},*result.highlight,{}});
    for(auto& header:result.headers)
        header.decoration.push_back({DecorationKind::FillRect,{226,230,234,255},header.divider,{}});
    for(auto& row:result.rows) {
        const auto row_bounds = !row.cells.empty() &&
                row.cells.front().text_bounds.width > 0.0F &&
                row.cells.front().text_bounds.height > 0.0F
            ? Rect{result.scroll.viewport.x + metric(2),
                   row.cells.front().text_bounds.y,
                   std::max(0.0F, result.scroll.viewport.width - metric(4)),
                   row.cells.front().text_bounds.height}
            : Rect{};
        if (!row.selection && hovered_index && *hovered_index == row.model_index &&
            row_bounds.width > 0.0F && row_bounds.height > 0.0F)
            row.decoration.push_back(
                {DecorationKind::FillRect,{232,244,255,255},row_bounds,{}});
        if(row.selection) {
            // DWTと同様、Windowsの非アクティブな一
            // 覧選択の色を使う。 淡い青の塗りと青いフォーカ
            // ス枠にし、濃い青で黒文字を読みにくくしない。
            row.decoration.push_back(
                {DecorationKind::FillRect,{204,232,255,255},*row.selection,{}});
            row.decoration.push_back(
                {DecorationKind::StrokeRect,{0,120,215,255},*row.selection,{}});
        }
        if(row.expander) {
            auto arrow_bounds = *row.expander;
            // DWTのネイティブの展開印とは異なり、これはフォントの字形
            // 。 行・当たり判定を動かさず、見た目の基線の低さを補正する
            // 。 Web・デスクトップとも共通の倍率適用済み表示を使う。
            arrow_bounds.y -= metric(2);
            row.expander_label=DecorationTextPresentation{
                row.expanded?"▼":"▶",arrow_bounds,TextAlignment::Center};
        }
        for(auto& cell:row.cells) {
            if(cell.grid) cell.decoration.push_back({DecorationKind::StrokeRect,{145,145,145,255},*cell.grid,{}});
            if(cell.check_box) cell.decoration.push_back({DecorationKind::StrokeRect,{145,145,145,255},*cell.check_box,{}});
            if(cell.check_mark) cell.decoration.push_back({DecorationKind::FillRect,
                cell.check_grayed ? Color{145,145,145,255} : Color{49,106,197,255},
                *cell.check_mark,{}});
            cell.foreground=cell.enabled?authored_foreground:Color{130,130,130,255};
        }
    }
    result.cursor_decoration=prepare_cursor_decoration(result.cursor);
    result.scroll_decoration=prepare_scroll_decoration(result.scroll,view.get());
    return result;
}

std::string item_comment_tooltip(const WidgetPtr& view,const Point pointer,
                                 const ItemViewPresentation& presentation) {
    const auto tree=std::dynamic_pointer_cast<TreeView>(view);
    const auto index=hit_test_item_view(view,pointer);
    const auto table=std::dynamic_pointer_cast<TableView>(view);
    if ((!tree && !table) || !index) return {};
    const auto row=std::find_if(presentation.rows.begin(),presentation.rows.end(),
        [index](const ItemRowPresentation& value) {
            return value.model_index==*index && value.comment_bounds.has_value();
        });
    if (row==presentation.rows.end()) return {};
    const auto text=comment_tooltip_at(row->comment_text,pointer,
                                        *row->comment_bounds);
    return std::string(text);
}

TabViewPresentation prepare_tab_view(const TabView& source,bool hovered,Point pointer) {
    const auto tabs=snapshot_tab_layout(source);
    TabViewPresentation result; result.bounds=tabs.bounds;
    result.content=layout_tab_content(tabs); result.geometry=layout_tab_headers(tabs);
    const auto bounds=result.bounds;
    result.baseline={bounds.x,result.content.y-1,bounds.width,1};
    result.decoration={{DecorationKind::FillRect,{250,250,250,255},bounds,{}},
        {DecorationKind::FillRect,{145,145,145,255},result.baseline,{}}};
    for (const auto& header:result.geometry.items) {
        if (header.index>=tabs.pages.size()) continue;
        const auto& page=tabs.pages[header.index]; const auto tab=header.bounds;
        TabItemPresentation item; item.index=header.index; item.bounds=tab; item.close=header.close;
        item.selected=tabs.selected_index==header.index;
        item.hot=hovered && point_inside(tab,pointer); item.icon=page.icon; item.title=display_text_without_mnemonic(page.title);
        const float leading=metric(resolved_tab_horizontal_padding(tabs.header_horizontal_padding,5));
        const float trailing=metric(resolved_tab_horizontal_padding(tabs.header_horizontal_padding,3));
        float x=tab.x+leading;
        if (page.icon) {
            const float size=metric(16);
            item.icon_bounds={x,tab.y+std::max(1.0F,(tab.height-size)*.5F),size,size}; x+=size+metric(2);
        }
        item.text_bounds={x,tab.y,std::max(0.0F,tab.x+tab.width-x-header.close.width-trailing),tab.height};
        item.decoration=prepare_tab_header_decoration(tab,item.selected,item.hot);
        item.labels.push_back({item.title,item.text_bounds});
        if(item.close.width>0) item.labels.push_back({"×",item.close,TextAlignment::Center});
        result.headers.push_back(std::move(item));
    }
    if(result.geometry.previous.width>0) {
        const auto navigation=[&](Rect r,std::string label,bool enabled) {
            result.navigation.push_back({r,prepare_tab_navigation_decoration(r),
                {std::move(label),r,TextAlignment::Center,enabled?Color{24,24,24,255}:Color{130,130,130,255}}});
        };
        navigation(result.geometry.previous,"◀",result.geometry.can_previous);
        navigation(result.geometry.next,"▶",result.geometry.can_next);
    }
    if (tabs.selected_index && *tabs.selected_index<tabs.pages.size()) result.selected_page=tabs.pages[*tabs.selected_index].content;
    return result;
}
} // namespace cwxeditor::lib
