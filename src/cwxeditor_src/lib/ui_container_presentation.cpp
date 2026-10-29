#include "ui_container_presentation.hpp"
#include "ui_child_order.hpp"
#include "ui_layout.hpp"
#include "ui_metrics.hpp"
#include <algorithm>

namespace cwxeditor::lib {
ContainerPresentation prepare_container_presentation(const Container& container) {
    ContainerPresentation result;
    result.viewport=container.bounds();
    if (dynamic_cast<const ScrollView*>(&container) || dynamic_cast<const Dialog*>(&container)) {
        result.scroll=item_scroll_geometry(container);
        result.viewport=result.scroll->viewport;
        result.scroll_decoration=prepare_scroll_decoration(*result.scroll,&container);
    }
    if (const auto* expand=dynamic_cast<const ExpandBar*>(&container)) {
        result.expandable=true;
        const auto children=expand->children();
        std::vector<ExpandItemMeasurement> measurements;
        const float arrow_inset=scaled_ui_metric(3), arrow_width=scaled_ui_metric(18);
        const float title_inset=scaled_ui_metric(21), title_padding=scaled_ui_metric(24);
        // 最初の字形計測の前に、区画の一覧全体を複製する。 提供処理が
        // 元の ExpandBar を置換・破棄する場合があるため。
        for (std::size_t i=0;i<children.size();++i) {
            const auto& child=children[i];
            if (!child || !child->visible() || std::dynamic_pointer_cast<Window>(child)) continue;
            ExpandItemPresentation section;
            section.index=i; section.bounds=child->bounds();
            section.title=expand_item_title(*child); section.style=prepare_widget_style(*child);
            section.open=expand_item_is_open(*expand,i);
            if (section.open) {
                if(section.style.background) section.body_decoration.push_back(
                    {DecorationKind::FillRect,*section.style.background,section.bounds,{}});
                if (const auto group=std::dynamic_pointer_cast<Container>(child))
                    section.children=presentation_children(*group);
                else section.children.push_back(child);
            }
            measurements.push_back(prepare_expand_item_measurement(*child));
            result.sections.push_back(std::move(section));
        }
        for (std::size_t i=0;i<result.sections.size();++i) {
            auto& section=result.sections[i];
            section.header=section.bounds;
            section.header.height=std::max(0.0F,std::min(section.header.height,
                measure_expand_item(measurements[i]).header_height));
            const auto r=section.bounds, h=section.header;
            section.body={r.x,r.y+h.height,r.width,std::max(0.0F,r.height-h.height)};
            section.arrow={h.x+arrow_inset,h.y,arrow_width,h.height};
            section.title_bounds={h.x+title_inset,h.y,std::max(0.0F,h.width-title_padding),h.height};
            section.decoration={{DecorationKind::FillRect,{235,238,242,255},section.header,{}},
                {DecorationKind::StrokeRect,{145,145,145,255},section.bounds,{}}};
            section.labels={{section.open?"▼":"▶",section.arrow,TextAlignment::Center,section.style.foreground},
                {section.title,section.title_bounds,TextAlignment::Start,section.style.foreground,2}};
        }
        return result;
    }
    result.children=presentation_children(container);
    if (const auto* bar=dynamic_cast<const CoolBar*>(&container); bar && !bar->locked) {
        for (const auto& child:result.children) {
            const auto bounds=child->bounds();
            const auto x=cool_bar_grip_bounds(*child).x+scaled_ui_metric(3);
            const Rect grip{x,bounds.y+scaled_ui_metric(2),scaled_ui_metric(1),
                std::max(0.0F,bounds.height-scaled_ui_metric(4))};
            if (grip.height<=0.0F) continue;
            result.grips.push_back(grip);
            result.decoration.push_back({DecorationKind::FillRect,{145,145,145,255},grip,{}});
        }
    }
    return result;
}
} // namespace cwxeditor::lib
