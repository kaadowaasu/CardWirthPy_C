#include "ui_chrome_presentation.hpp"
#include "ui_metrics.hpp"
#include <algorithm>

namespace cwxeditor::lib {
WindowChromePresentation prepare_window_chrome(const Window& window,Rect bounds,
    bool embedded,bool show_caption) {
    WindowChromePresentation result;
    result.bounds=bounds; result.content=bounds; result.clear_surface=!embedded;
    result.opacity=window.opacity;
    result.transparent_background=window.overlay_surface;
    if (result.transparent_background) {
        result.background={0,0,0,0};
        return result; // ホバープレビューには、ALL の見出しや本体の塗りつぶしを付けない。
    }
    if(embedded &&
       window.host_mode==WindowHostMode::EmbeddedDialog) {
        // ALL仕様: 子・孫ダイアログも外周の茶色い枠で背面と区別する。
        // エンジン側の枠と同じく、元の3px基準の半分に揃える。
        result.boundary_width=scaled_ui_metric(1.5F);
    }
    if(embedded && show_caption &&
       window.host_mode==WindowHostMode::EmbeddedDialog) {
        WindowCaptionPresentation caption;
        caption.geometry=layout_hosted_caption(bounds,true,hosted_minimizable(window),
                                               window.caption_buttons);
        caption.title=window.title;
        caption.icon=window.icon;
        const auto icon_size=scaled_ui_metric(16);
        // 左寄せの取り消し・やり直しがある場合、アイコンとタイトル
        // はその右から始める。 なければ見出しの左端から始める。
        const float title_left=caption.geometry.redo_button.width>0
            ? caption.geometry.redo_button.x+caption.geometry.redo_button.width+scaled_ui_metric(10)
            : caption.geometry.caption.x;
        caption.icon_bounds={title_left+scaled_ui_metric(5),
            caption.geometry.caption.y+(caption.geometry.caption.height-icon_size)*.5F,icon_size,icon_size};
        caption.font=effective_editor_widget_font();
        caption.title_origin={title_left+scaled_ui_metric(caption.icon ? 26 : 8),
                              caption.geometry.caption.y};
        const float title_right=caption.geometry.minimize_button.width>0
            ? caption.geometry.minimize_button.x : caption.geometry.close_button.x;
        caption.title_bounds={caption.title_origin.x,caption.title_origin.y,
            std::max(0.0F,title_right-caption.title_origin.x-scaled_ui_metric(10)),
            caption.geometry.caption.height};
        caption.title_layout=prepare_aligned_text(caption.title,caption.title_bounds,
            TextAlignment::Start,0,caption.font);
        if(caption.title_layout) caption.title_origin=caption.title_layout->origin;
        if(caption.geometry.close_button.width>0)
            caption.close_layout=prepare_aligned_text(caption.close_text,caption.geometry.close_button,
                TextAlignment::Center,0,caption.font);
        if(caption.geometry.minimize_button.width>0)
            caption.minimize_layout=prepare_aligned_text(caption.minimize_text,
                caption.geometry.minimize_button,TextAlignment::Center,0,caption.font);
        if(caption.geometry.undo_button.width>0) {
            // 各ボタンの左にアイコンを置き、残りの領域に文字を描く。
            caption.undo_icon=hosted_caption_undo_icon();
            caption.redo_icon=hosted_caption_redo_icon();
            const auto history_size=scaled_ui_metric(16);
            const auto place=[&](const Rect& button,const ResourceId icon,Rect& icon_bounds) {
                if(!icon) return button;
                icon_bounds={button.x+scaled_ui_metric(6),
                    button.y+(button.height-history_size)*.5F,history_size,history_size};
                const float left=icon_bounds.x+history_size+scaled_ui_metric(2);
                return Rect{left,button.y,std::max(0.0F,button.x+button.width-left),button.height};
            };
            caption.undo_layout=prepare_aligned_text(caption.undo_text,
                place(caption.geometry.undo_button,caption.undo_icon,caption.undo_icon_bounds),
                TextAlignment::Center,0,caption.font);
            caption.redo_layout=prepare_aligned_text(caption.redo_text,
                place(caption.geometry.redo_button,caption.redo_icon,caption.redo_icon_bounds),
                TextAlignment::Center,0,caption.font);
        }
        result.content=caption.geometry.content; result.caption=std::move(caption);
    }
    return result;
}
WidgetStylePresentation prepare_widget_style(const Widget& widget) {
    WidgetStylePresentation result{widget.bounds(),
        widget.enabled() ? widget.foreground().value_or(Color{24,24,24,255}) : Color{130,130,130,255},
        widget.background(),{}};
    result.font=effective_editor_widget_font(widget.font() ? &*widget.font() : nullptr);
    result.kind=widget.kind();
    if (const auto* window=dynamic_cast<const Window*>(&widget); window && window->overlay_surface)
        result.background.reset();
    return result;
}
std::optional<ChromePresentation> prepare_widget_chrome(const Widget& widget,ChromeInteraction interaction) {
    ChromePresentation result;
    const auto r=widget.bounds();
    if (!widget.enabled()) interaction={};
    const auto focus=[&] {
        const float inset=scaled_ui_metric(3);
        result.focus_bounds=Rect{r.x+inset,r.y+inset,std::max(0.0F,r.width-2*inset),std::max(0.0F,r.height-2*inset)};
    };
    if (const auto* group=dynamic_cast<const GroupBox*>(&widget)) {
        result.text=group->title; result.border=true;
        if (!group->title.empty()) {
            const auto* font = group->font() ? &*group->font() : nullptr;
            const float caption_height = ui_text_line_height(
                kEditorUiMetrics.group_caption_padding, 4.0F, font);
            const float caption_width = std::min(
                std::max(0.0F, r.width - scaled_ui_metric(16.0F)),
                ui_text_width(group->title, 0.0F, font));
            const float x = r.x + scaled_ui_metric(8.0F);
            result.title_origin=Point{x,r.y};
            // タイトルの境界は、上枠の切れ目にもなる。 D版・
            // SWTと同様、Groupの見出しは完成した矩形の
            // 内側ではなく、上枠をまたぐラベルとして表示する。
            result.text_bounds={x,r.y,caption_width,caption_height};
        }
        return result;
    }
    const auto* label=dynamic_cast<const TextWidget*>(&widget);
    if (!label) return {};
    const auto kind=widget.kind();
    if (kind==ComponentKind::Label || kind==ComponentKind::Link ||
        kind==ComponentKind::StatusBar || kind==ComponentKind::ToolTip) {
        result.text=label->text; result.alignment=label->alignment;
        result.wrap=label->wrap;
        result.multiline=result.wrap || result.text.find_first_of("\r\n")!=std::string::npos;
        result.text_bounds=r; result.text_padding=scaled_ui_metric(kEditorUiMetrics.label_horizontal_padding);
        if (kind==ComponentKind::StatusBar) { result.fill=Color{235,235,235,255}; result.border=true; }
        if (kind==ComponentKind::Link) {
            if (widget.enabled() && !widget.foreground())
                result.foreground=interaction.pressed ? Color{0,51,153,255} :
                    interaction.hovered ? Color{0,102,204,255} : Color{0,80,180,255};
            if (interaction.focused) focus();
        }
        return result;
    }
    const auto* button=dynamic_cast<const Button*>(&widget);
    if (!button) return {};
    result.default_button=button->default_button;
    result.cancel_button=button->cancel_button; // Escapeの操作を表す。独自のネイティブ色は作らない。
    if (!widget.enabled()) { result.border_color={180,180,180,255}; result.mark_color={150,150,150,255}; }
    if (interaction.focused) focus();
    result.text=display_text_without_mnemonic(button->text);
    result.alignment=kind==ComponentKind::CheckBox || kind==ComponentKind::RadioButton ?
        TextAlignment::Start : TextAlignment::Center;
    result.text_padding=4.0F;
    if (kind==ComponentKind::Button || kind==ComponentKind::ToggleButton) {
        result.fill=Color{238,238,238,255}; result.border=true;
        if (const auto* toggle=dynamic_cast<const ToggleButton*>(button); toggle && toggle->checked)
            result.fill=widget.enabled() ? Color{210,225,248,255} : Color{220,220,220,255};
        if (interaction.pressed) { result.fill=Color{204,224,247,255}; result.border_color={0,84,153,255}; }
        else if (interaction.hovered) { result.fill=Color{225,235,248,255}; result.border_color={0,120,215,255}; }
        // default_button はEnterで実行する役割であり
        // 、常時ホバーしている状態ではない。 入力欄・一覧にフォーカスが
        // ある間も青く塗ると、OKボタンが常に指されているように見える。
    }
    float tx=r.x+scaled_ui_metric(7);
    if (const auto* toggle=dynamic_cast<const ToggleButton*>(button);
        toggle && (kind==ComponentKind::CheckBox || kind==ComponentKind::RadioButton)) {
        const auto size=scaled_ui_metric(14);
        const Rect mark{r.x+scaled_ui_metric(5),r.y+(r.height-size)*.5F,size,size};
        result.mark=mark; result.radio=kind==ComponentKind::RadioButton;
        if (interaction.pressed) result.mark_fill=Color{204,224,247,255};
        else if (interaction.hovered) result.mark_fill=Color{225,235,248,255};
        if (const auto* check=dynamic_cast<const CheckBox*>(toggle); check && check->indeterminate) {
            const auto inset=scaled_ui_metric(3);
            const auto height=scaled_ui_metric(3);
            result.checked_mark=Rect{mark.x+inset,mark.y+(mark.height-height)*.5F,mark.width-2*inset,height};
        } else if (toggle->checked) {
            const auto inset=scaled_ui_metric(result.radio ? 4.0F : 3.0F);
            result.checked_mark=Rect{mark.x+inset,mark.y+inset,mark.width-2*inset,mark.height-2*inset};
        }
        tx=r.x+scaled_ui_metric(24);
        // ALL のチェックボックスの文字は、箱のすぐ右に2px間隔で置き、余
        // 分な余白を設けない。 ラジオボタンと単独版は、従来の間隔を維持する。
        if (kind==ComponentKind::CheckBox && editor_window_host_options().all_mode) {
            tx=r.x+scaled_ui_metric(21);
            result.text_padding=0.0F;
        }
    }
    // 既存のアイコン位置と送り量を維持する。 表示状態の取
    // 得によって、リソース・マスク・倍率の方針は変えない。
    result.icon=button->icon;
    const auto size=scaled_ui_metric(16);
    result.icon_bounds={tx+scaled_ui_metric(button->icon_offset_x),
        r.y+std::max(scaled_ui_metric(2),(r.height-size)*.5F),size,size};
    // 画像だけの押しボタン・切り替えボタンは、画像を
    // 中央に描く。 文字の原点へ左寄せすると、幅広い
    // ツールボタンでアイコンが中心からずれてしまう。
    if ((kind==ComponentKind::Button || kind==ComponentKind::ToggleButton) &&
        result.icon && result.text.empty())
        result.icon_bounds.x=r.x+(r.width-size)*.5F+scaled_ui_metric(button->icon_offset_x);
    const auto advance=result.icon ? scaled_ui_metric(19) : 0;
    result.text_bounds={tx+advance,r.y,std::max(0.0F,r.width-(tx-r.x)-advance),r.height};
    if ((kind==ComponentKind::Button || kind==ComponentKind::ToggleButton) &&
        button->text_horizontal_padding && !button->icon) {
        result.text_bounds=r;
        result.text_padding=scaled_ui_metric(std::max(0.0F, *button->text_horizontal_padding));
    }
    return result;
}
} // namespace cwxeditor::lib
