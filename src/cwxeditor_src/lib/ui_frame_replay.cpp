#include "ui_frame_replay.hpp"
#include "ui_decoration_presentation.hpp"
#include <algorithm>
#include <tuple>
#include <cmath>
#include <unordered_map>

namespace cwxeditor::lib {
namespace {
Rect inward_pixel_clip(Rect bounds) noexcept {
    // SDLの汎用クリップ変換は意図的に外側へ丸める。通
    // 常の再描画範囲には適するが、 タブ内容の原点が小数
    // だと、選択ページの一画素が見出し・基線へ入り、 動
    // くエリア・戦闘キャンバスでその行がちらつく。 描画
    // 先へ渡す前に、この表示範囲の境界を内側へ揃える。
    const float left=std::ceil(bounds.x),top=std::ceil(bounds.y);
    const float right=std::floor(bounds.x+std::max(0.0F,bounds.width));
    const float bottom=std::floor(bounds.y+std::max(0.0F,bounds.height));
    return {left,top,std::max(0.0F,right-left),std::max(0.0F,bottom-top)};
}
class Clip {
public:
    Clip(FrameRasterSink& sink,Rect bounds):sink_(sink) {sink_.push_clip(bounds);}
    ~Clip() {sink_.pop_clip();}
    Clip(const Clip&)=delete;
    Clip& operator=(const Clip&)=delete;
private:
    FrameRasterSink& sink_;
};
class Replay {
    const WindowFramePresentation& frame;
    FrameRasterSink& sink;
    bool valid{true};
    std::unordered_map<const Widget*,const CanvasFramePresentation*> paints;
    void icon(ResourceId id,Rect bounds) {
        const auto found=frame.resources.find(id);
        if (frame.drawing_prepared && found!=frame.resources.end() && found->second)
            // 小数のUI倍率も含め、既存のマスク付きピクセルアートの経路を維持する
            // 。 変更するのはリソースの所有関係だけで、マスク・補間は変えない。
            sink.owned_image(found->second,0U,std::nullopt,bounds,Interpolation::Nearest);
    }
    void disabled_icon(ResourceId id,Rect bounds) {
        constexpr std::uint8_t opacity=96;
        // 埋め込みのレンダラーは、一時的な不透明度レイヤーに未対応で
        // もよい。 その場合も無効なコマンドを判別できるようにし、
        // ALL のツールバーが空の枠だけにならないようにする。
        if(!sink.begin_layer(bounds,opacity)) { icon(id,bounds); return; }
        icon(id,bounds);
        if(!sink.end_layer(true)) valid=false;
    }
    FontSpec font(const WidgetPtr& widget) const {
        const auto style=frame.styles.find(widget.get());
        return style!=frame.styles.end() && style->second.font ? *style->second.font : FontSpec{};
    }
    void placed(std::string_view value,const std::optional<AlignedTextLayout>& placement,
                Color color,const FontSpec& face) {
        if (!placement) return;
        Clip clip(sink,placement->clip);
        sink.text(value,placement->origin,color,face);
    }
    void label(const DecorationTextPresentation& value,const FontSpec& face) {
        placed(value.text,value.text_layout,value.color,face);
    }
    void text_edit(const std::shared_ptr<TextEdit>& edit) {
        const auto found=frame.text.find(edit.get());
        if(found==frame.text.end()) return;
        const auto& view=found->second;
        sink.decoration(view.decoration); sink.decoration(view.scroll_decoration);
        Clip clip(sink,view.inner);
        const auto face=font(edit);
        for(const auto& line:view.visible_lines) {
            sink.decoration(line.decoration);
            sink.text(line.text,line.origin,view.foreground,face);
            if (line.selection) {
                // 選択範囲外は指定された文字色を保ち、青い選択
                // 背景の上ではネイティブ・D版形式の反転文字色
                // を使う。 保持した行を選択範囲のクリップで描
                // き、境界でもUTF-8の文字整形を維持する。
                Clip selection(sink,*line.selection);
                sink.text(line.text,line.origin,view.selection_foreground,face);
            }
        }
        sink.decoration(view.caret_decoration);
    }
    void inline_edit() {
        if(!frame.inline_editor) return;
        text_edit(frame.inline_text); sink.decoration(frame.inline_decoration);
        if(frame.inline_label) label(*frame.inline_label,font(frame.inline_text));
        if(frame.inline_number) sink.decoration(frame.inline_number->decoration);
    }
    void chrome(const WidgetPtr& widget,const WidgetStylePresentation& style) {
        const auto found=frame.chrome.find(widget.get());
        if(found==frame.chrome.end()) return;
        const auto& view=found->second;
        if(const auto d=frame.decorations.find(widget.get());d!=frame.decorations.end()) sink.decoration(d->second);
        const auto face=font(widget);
        const auto foreground=view.foreground.value_or(style.foreground);
        if(view.paragraph) {
            Clip clip(sink,view.text_bounds);
            for(const auto& line:view.paragraph->lines)
                sink.text(line.text,line.placement.origin,foreground,face);
        } else if(view.title_origin) {
            Clip clip(sink,view.text_bounds);
            sink.text(view.text,*view.title_origin,foreground,face);
        } else {
            if(view.icon) icon(view.icon,view.icon_bounds);
            placed(view.text,view.text_layout,foreground,face);
        }
    }
    void strip(const WidgetPtr& widget) {
        const auto found=frame.strips.find(widget.get());
        if(found==frame.strips.end()) return;
        const auto& view=found->second; const auto face=font(widget);
        const auto draw = [&] {
            sink.decoration(view.decoration);
            for(const auto& item:view.items) {
                Clip clip(sink,item.bounds); sink.decoration(item.decoration);
                if(item.separator) continue;
                if(item.icon) {
                    if(item.enabled) icon(item.icon,item.icon_bounds);
                    else disabled_icon(item.icon,item.icon_bounds);
                }
                if(item.top_aligned || !item.icon) placed(item.text,item.text_layout,item.foreground,face);
                if(item.arrow) label(*item.arrow,face);
            }
            sink.decoration(view.overlay);
        };
        if (const auto toolbar=std::dynamic_pointer_cast<ToolBar>(widget);
            toolbar && toolbar->single_row) {
            // 狭い単独版のシーンペインでは、SWT.FLATと同様に末尾
            // のボタンを切り取り、 位置・寸法の操作欄の上へ描かない。
            Clip clip(sink,view.bounds);
            draw();
        } else draw();
    }
    void items(const WidgetPtr& widget) {
        const auto found=frame.item_views.find(widget.get());
        if(found==frame.item_views.end()) return;
        const auto& view=found->second; const auto face=font(widget);
        sink.decoration(view.decoration);
        {
            Clip clip(sink,view.scroll.viewport); sink.decoration(view.content_decoration);
            for(const auto& header:view.headers) {
                sink.decoration(header.decoration);
                placed(header.text,header.text_layout,header.foreground,face);
            }
            for(const auto& row:view.rows) {
                sink.decoration(row.decoration);
                if(row.expander_label) label(*row.expander_label,face);
                for(const auto& cell:row.cells) {
                    sink.decoration(cell.decoration);
                    if(cell.icon) icon(cell.icon,cell.icon_bounds);
                    if(cell.trailing_icon) icon(cell.trailing_icon,cell.trailing_icon_bounds);
                    if(cell.inline_editor) inline_edit();
                    else placed(cell.text,cell.text_layout,cell.foreground,face);
                }
                // D版の setupComment(Table) と同様、ネイ
                // ティブの行セルの後に診断を描き、コメントの印を最後に描く。
                if(row.warning_icon && row.warning_bounds) icon(row.warning_icon,*row.warning_bounds);
                else if(row.warning_label) label(*row.warning_label,face);
                if(row.comment_icon && row.comment_bounds) icon(row.comment_icon,*row.comment_bounds);
            }
        }
        sink.decoration(view.scroll_decoration);
    }
    void tabs(const std::shared_ptr<TabView>& widget) {
        const auto found=frame.tabs.find(widget.get());
        if(found==frame.tabs.end()) return;
        const auto& view=found->second; const auto face=font(widget);
        sink.decoration(view.decoration);
        if (!view.selected_page && frame.drawing_prepared) {
            const auto empty=frame.tab_empty_client_paints.find(widget.get());
            if (empty!=frame.tab_empty_client_paints.end()) {
                Clip clip(sink,inward_pixel_clip(view.content));
                sink.canvas(empty->second);
            }
        }
        for(const auto& header:view.headers) {
            Clip clip(sink,header.bounds); sink.decoration(header.decoration);
            if(header.icon) icon(header.icon,header.icon_bounds);
            for(const auto& text:header.labels) label(text,face);
        }
        for(const auto& navigation:view.navigation) {
            sink.decoration(navigation.decoration); label(navigation.label,face);
        }
        if(view.selected_page) {
            Clip clip(sink,inward_pixel_clip(view.content));
            this->widget(view.selected_page);
        }
    }
    void container(const std::shared_ptr<Container>& owner) {
        const auto found=frame.containers.find(owner.get());
        if(found==frame.containers.end()) return;
        const auto& view=found->second;
        {
            Clip clip(sink,view.viewport);
            if(view.expandable) {
                for(const auto& section:view.sections) {
                    sink.decoration(section.body_decoration);
                    {
                        Clip body_clip(sink,section.body);
                        for(const auto& child:section.children) widget(child);
                    }
                    sink.decoration(section.decoration);
                    const auto face=section.style.font.value_or(FontSpec{});
                    for(const auto& text:section.labels) label(text,face);
                }
            } else for(const auto& child:view.children) widget(child);
        }
        sink.decoration(view.scroll_decoration);
    }
    void popup(const PopupPresentation& view) {
        Clip clip(sink,view.bounds); const auto face=view.font.value_or(FontSpec{});
        sink.decoration({{DecorationKind::FillRect,view.background,view.bounds,{}},
                         {DecorationKind::StrokeRect,view.border,view.bounds,{}}});
        for(const auto& row:view.rows) {
            if(row.highlight) sink.decoration({{DecorationKind::FillRect,*row.highlight,row.bounds,{}}});
            if(row.separator) {
                sink.decoration({{DecorationKind::FillRect,view.border,*row.separator,{}}}); continue;
            }
            if(row.icon) icon(row.icon,row.icon_bounds);
            for (const auto& button : row.buttons)
                sink.decoration({{DecorationKind::FillRect,{242,242,242,255},button,{}},
                                 {DecorationKind::StrokeRect,view.border,button,{}}});
            for(const auto& text:row.text) placed(text.text,text.text_layout,text.color,face);
        }
        if (view.scroll_track)
            sink.decoration({{DecorationKind::FillRect,{240,240,240,255},*view.scroll_track,{}}});
        if (view.scroll_thumb)
            sink.decoration({{DecorationKind::FillRect,
                view.scroll_thumb_pressed ? kScrollThumbPressedColor : Color{166,166,166,255},
                *view.scroll_thumb,{}}});
    }
    void widget(const WidgetPtr& owner) {
        if(!owner || (owner!=frame.window && std::dynamic_pointer_cast<Window>(owner))) return;
        const auto found=frame.styles.find(owner.get());
        if(found==frame.styles.end()) return;
        const auto& style=found->second;
        switch(style.kind) {
        case ComponentKind::Window: case ComponentKind::Dialog: case ComponentKind::Panel:
        case ComponentKind::ScrollView: case ComponentKind::ExpandBar: case ComponentKind::SplitView:
        case ComponentKind::CoolBar:
            if(style.background) sink.decoration({{DecorationKind::FillRect,*style.background,style.bounds,{}}});
            if(style.kind==ComponentKind::CoolBar) {
                const auto c=frame.containers.find(static_cast<Container*>(owner.get()));
                if(c!=frame.containers.end()) sink.decoration(c->second.decoration);
            }
            break;
        case ComponentKind::GroupBox: case ComponentKind::Label: case ComponentKind::Link:
        case ComponentKind::StatusBar: case ComponentKind::ToolTip: case ComponentKind::Button:
        case ComponentKind::ToggleButton: case ComponentKind::CheckBox: case ComponentKind::RadioButton:
            chrome(owner,style); break;
        case ComponentKind::TextEdit: text_edit(std::static_pointer_cast<TextEdit>(owner)); break;
        case ComponentKind::NumberEdit: {
            const auto n=frame.numbers.find(static_cast<NumberEdit*>(owner.get()));
            if(n!=frame.numbers.end()) {
                sink.decoration(n->second.decoration);
                if(n->second.editor) text_edit(n->second.editor);
                for(const auto& value:n->second.labels) label(value,font(owner));
            }
            break;
        }
        case ComponentKind::ComboBox: {
            const auto c=frame.combo_controls.find(static_cast<ComboBox*>(owner.get()));
            if(c!=frame.combo_controls.end()) {
                sink.decoration(c->second.decoration);
                if(c->second.icon) icon(c->second.icon,c->second.icon_bounds);
                if(c->second.editor) text_edit(c->second.editor);
                for(const auto& value:c->second.labels) label(value,font(owner));
            }
            break;
        }
        case ComponentKind::MenuBar: case ComponentKind::ToolBar: strip(owner); break;
        case ComponentKind::ListView: case ComponentKind::TableView: case ComponentKind::TreeView: items(owner); break;
        case ComponentKind::TabView: tabs(std::static_pointer_cast<TabView>(owner)); return;
        case ComponentKind::ProgressBar: case ComponentKind::Slider: case ComponentKind::Separator: case ComponentKind::ColorWell:
            if(const auto d=frame.decorations.find(owner.get());d!=frame.decorations.end()) sink.decoration(d->second);
            break;
        case ComponentKind::Canvas: {
            break;
        }
        case ComponentKind::ImageView: {
            const auto i=frame.images.find(static_cast<ImageView*>(owner.get()));
            if(i!=frame.images.end() && frame.drawing_prepared) {
                const auto& view=i->second;
                if(view.viewport.content.width>0 && view.viewport.content.height>0) {
                    Clip clip(sink,view.viewport.content);
                    const auto draw=[&](const ImagePresentation& image) {
                        image.for_each_blit([&](const ImageBlitPresentation& b) {
                            sink.owned_image(image.image,image.frame,b.source,b.destination,image.interpolation);
                        });
                    };
                    if(view.layers.empty()) draw(view.drawing);
                    else for(const auto& image:view.layer_drawing) draw(image);
                }
                sink.decoration(view.decoration);
            }
            break;
        }
        default: break;
        }
        const auto paint = paints.find(owner.get());
        if (paint != paints.end()) {
            if (frame.drawing_prepared) sink.canvas(*paint->second);
            sink.decoration(paint->second->decoration);
        }
        // Canvas の編集欄は、記録済みの所有者の描画より上に保つ。
        if (owner->kind() == ComponentKind::Canvas && frame.inline_editor &&
            frame.inline_owner == owner.get()) inline_edit();
        if(const auto item=frame.item_views.find(owner.get());item!=frame.item_views.end()) sink.decoration(item->second.cursor_decoration);
        if(const auto c=std::dynamic_pointer_cast<Container>(owner)) container(c);
    }
public:
    Replay(const WindowFramePresentation& value,FrameRasterSink& target):frame(value),sink(target) {
        paints.reserve(frame.canvases.size());
        for(const auto& paint:frame.canvases) paints.emplace(paint.owner.get(),&paint);
    }
    bool run() {
        const bool isolated=frame.shell && frame.shell->opacity<255;
        if (isolated && frame.shell->clear_surface) sink.clear({0,0,0,0});
        if (isolated && frame.shell->opacity==0) return true;
        if (isolated && !sink.begin_layer(frame.shell->bounds,frame.shell->opacity)) return false;
        struct LayerGuard {
            FrameRasterSink& sink; bool active;
            ~LayerGuard() {if(active) (void)sink.end_layer(false);}
        } layer{sink,isolated};
        if(frame.shell) {
            const auto& shell=*frame.shell;
            if(shell.clear_surface && !isolated) sink.clear(shell.background);
            else if (!shell.transparent_background)
                sink.decoration({{DecorationKind::FillRect,shell.background,shell.bounds,{}}});
            if(shell.caption) {
                const auto& caption=*shell.caption;
                sink.decoration({
                    {DecorationKind::FillRect,caption.background,caption.geometry.caption,{}},
                    {DecorationKind::FillRect,caption.border,
                     {caption.geometry.caption.x,
                      caption.geometry.caption.y+std::max(0.0F,caption.geometry.caption.height-1.0F),
                      caption.geometry.caption.width,
                      std::min(1.0F,caption.geometry.caption.height)}, {}}});
                {
                    Clip clip(sink,caption.geometry.caption);
                    icon(caption.icon,caption.icon_bounds);
                }
                {
                    Clip clip(sink,caption.title_bounds);
                    placed(caption.title,caption.title_layout,caption.foreground,caption.font);
                }
                if(caption.geometry.close_button.width>0) {
                    sink.decoration({{DecorationKind::FillRect,caption.background,
                                      caption.geometry.close_button,{}}});
                    placed(caption.close_text,caption.close_layout,caption.foreground,caption.font);
                }
                if(caption.geometry.minimize_button.width>0) {
                    sink.decoration({{DecorationKind::FillRect,caption.background,
                                      caption.geometry.minimize_button,{}}});
                    placed(caption.minimize_text,caption.minimize_layout,
                           caption.foreground,caption.font);
                }
                if(caption.geometry.undo_button.width>0) {
                    // 通常のボタンと同様に枠を描き、見出しの高さを
                    // 埋める。 上下のバーとの間に余白は置かない。
                    for(const auto& [button,text,layout,history_icon,icon_bounds] : {
                            std::tuple{caption.geometry.undo_button,&caption.undo_text,&caption.undo_layout,
                                       caption.undo_icon,caption.undo_icon_bounds},
                            std::tuple{caption.geometry.redo_button,&caption.redo_text,&caption.redo_layout,
                                       caption.redo_icon,caption.redo_icon_bounds}}) {
                        const Rect face=button;
                        sink.decoration({{DecorationKind::FillRect,{240,240,240,255},face,{}},
                                         {DecorationKind::StrokeRect,caption.border,face,{}}});
                        if(history_icon) icon(history_icon,icon_bounds);
                        placed(*text,*layout,caption.foreground,caption.font);
                    }
                }
            }
        }
        widget(frame.window);
        if(!valid) return false;
        if (frame.shell && frame.shell->boundary_width > 0) {
            const auto& shell=*frame.shell;
            const auto r=shell.bounds;
            const float w=std::min(shell.boundary_width,std::min(r.width,r.height)*0.5F);
            // 端に揃った子が枠を消さないよう、本体の後で枠を描く。
            sink.decoration({
                {DecorationKind::FillRect,shell.boundary_color,{r.x,r.y,r.width,w},{}},
                {DecorationKind::FillRect,shell.boundary_color,{r.x,r.y+r.height-w,r.width,w},{}},
                {DecorationKind::FillRect,shell.boundary_color,{r.x,r.y,w,r.height},{}},
                {DecorationKind::FillRect,shell.boundary_color,{r.x+r.width-w,r.y,w,r.height},{}}});
        }
        if(frame.combo) popup(frame.combo->popup);
        for(const auto& menu:frame.menus) popup(menu);
        if(frame.tooltip) {
            const auto& view=*frame.tooltip;
            Clip clip(sink,view.bounds); sink.decoration(view.decoration);
            if (view.title) {
                if (view.title->icon) icon(view.title->icon,view.title->icon_bounds);
                Clip title_clip(sink,view.title->text_clip);
                sink.text(view.title->text,view.title->origin,view.foreground,view.title->font);
            }
            for(const auto& line:view.lines) sink.text(line.text,line.origin,view.foreground,view.font);
        }
        if (isolated) {layer.active=false; return sink.end_layer(true);}
        return true;
    }
};
} // namespace
bool replay_window_frame(const WindowFramePresentation& frame,FrameRasterSink& sink) {return Replay(frame,sink).run();}
} // namespace cwxeditor::lib
