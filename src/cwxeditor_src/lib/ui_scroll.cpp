#include "ui_scroll.hpp"
#include "ui_layout.hpp"
#include "ui_metrics.hpp"

#include <cmath>

namespace cwxeditor::lib {
namespace {
struct TextScrollRequest {
    std::optional<float> x,y;
    bool relative{},wheel{};
};
struct TextScrollResult {
    ItemScrollGeometry geometry;
    bool moved{},invalidated{};
};
TextScrollResult resolve_text_scroll(std::shared_ptr<TextEdit> edit,
                                    ScrollTextProvider provider,TextScrollRequest request) {
    TextScrollResult result;
    try {
    if (!edit || !edit->enabled() || !edit->visible() || !edit->multiline || !provider) return result;
    const auto scale_generation=editor_ui_text_scale_generation();
    const auto access=provider(edit);
    if ((access.current && !access.current()) || scale_generation!=editor_ui_text_scale_generation()) {
        result.invalidated=true; return result;
    }
    if (!access.state || !access.measure || !std::isfinite(access.line_height) || access.line_height<=0) return result;
    const TextEditSnapshot before(*edit,*access.state);
    const auto current=[&] {
        return (!access.current || access.current()) && scale_generation==editor_ui_text_scale_generation() &&
            before.matches(*edit,*access.state);
    };
    if (!before.edit.enabled() || !before.edit.visible() || !before.edit.multiline) return result;
    if ((request.x && !std::isfinite(*request.x)) || (request.y && !std::isfinite(*request.y))) return result;
    const bool horizontal=before.edit.horizontal_scroll && !before.edit.wrap;
    const bool vertical=before.edit.vertical_scroll;
    auto composition=project_text_composition(before.edit,before.state);
    const auto& display=composition ? composition->edit : before.edit;
    auto prepared=composition ? composition->state : before.state;
    const auto inner=text_edit_inner_bounds(display);
    const auto lines=layout_text_lines(display,inner.width,access.measure);
    if (!current()) {result.invalidated=true; return result;}
    const float unit=request.wheel ? access.line_height*3 : 1;
    if (request.x && horizontal)
        prepared.scroll_x=(request.relative ? prepared.scroll_x : 0)+*request.x*unit;
    if (request.y && vertical)
        prepared.scroll_y=(request.relative ? prepared.scroll_y : 0)+*request.y*unit;
    clamp_text_edit_scroll(display,prepared,lines,inner,access.line_height);
    result.geometry=text_edit_scroll_geometry(display,lines,access.line_height,prepared);
    if (!current()) {result={};result.invalidated=true;return result;}
    result.moved=before.state.scroll_x!=prepared.scroll_x || before.state.scroll_y!=prepared.scroll_y;
    // 表示領域のオフセットだけを反映する。 スクロールで未確定文
    // 字を確定したり、選択を変えたり、編集通知を出したりしない。
    access.state->scroll_x=prepared.scroll_x; access.state->scroll_y=prepared.scroll_y;
    return result;
    } catch (...) {
        // 入力の提供処理はアプリのコード。例外時はこの古い操
        // 作を消費し、 文書・選択・オフセットを変更しない。
        result={}; result.invalidated=true; return result;
    }
}
} // namespace

ScrollBarAccess text_scrollbar_access(const std::shared_ptr<TextEdit>& edit,
                                     ScrollTextProvider provider) {
    if (!edit || !edit->multiline || !provider) return {};
    if (!edit->vertical_scroll && !(edit->horizontal_scroll && !edit->wrap)) {
        (void)resolve_text_scroll(edit,provider,{});
        return {};
    }
    const std::weak_ptr<TextEdit> weak = edit;
    const auto resolve = [weak,provider](std::optional<float> position,bool relative,bool vertical) {
        return resolve_text_scroll(weak.lock(),provider,
            {vertical?std::nullopt:position,vertical?position:std::nullopt,relative,false});
    };
    const auto current=[weak,provider] {
        try {
            const auto target=weak.lock();
            if (!target || !target->enabled() || !target->visible() || !target->multiline) return false;
            const auto access=provider(target);
            return access.state && access.measure && (!access.current || access.current());
        } catch (...) { return false; }
    };
    return {[resolve] { return resolve({},false,true).geometry; },
        [resolve](bool vertical,float value,bool relative) {
            return std::isfinite(value) && resolve(value,relative,vertical).moved;
        },current};
}

bool UiScrollStateMachine::wheel(const WidgetPtr& target, Point delta,
                                 Modifiers modifiers, float row_height,
                                 const ScrollTextProvider& text) {
    if ((static_cast<std::uint32_t>(modifiers) &
         static_cast<std::uint32_t>(Modifiers::Shift)) != 0)
        delta = {delta.x + delta.y, 0};
    return apply(target, {-delta.x,-delta.y}, true, row_height, text);
}

bool UiScrollStateMachine::pan(const WidgetPtr& target, Point displacement,
                               float row_height, const ScrollTextProvider& text) {
    return apply(target, {-displacement.x,-displacement.y}, false, row_height, text);
}

bool UiScrollStateMachine::apply(const WidgetPtr& target, Point delta, bool wheel,
                                 float row_height, const ScrollTextProvider& text, bool* invalidated) {
    if (invalidated) *invalidated=false;
    if (!target || !target->visible() || !target->enabled() ||
        !std::isfinite(delta.x) || !std::isfinite(delta.y) ||
        !std::isfinite(row_height) || row_height <= 0 ||
        (delta.x == 0 && delta.y == 0)) return false;
    if (std::dynamic_pointer_cast<ListView>(target) ||
        std::dynamic_pointer_cast<TableView>(target) ||
        std::dynamic_pointer_cast<TreeView>(target)) {
        row_height = item_row_height(*target, row_height);
        const float unit = wheel ? row_height * 3 : 1;
        bool stale=false;
        const bool moved=items_.scroll(target,{delta.x*unit,delta.y*unit},row_height,&stale);
        if (invalidated) *invalidated=stale;
        return moved;
    }
    if (const auto scroll = std::dynamic_pointer_cast<ScrollView>(target)) {
        const bool has_custom_step = std::isfinite(scroll->wheel_step) &&
            scroll->wheel_step > 0.0F;
        const bool per_event_step = wheel && has_custom_step && scroll->wheel_step_per_event;
        const float unit = wheel
            ? (has_custom_step ? scroll->wheel_step * editor_ui_text_scale()
                               : scaled_ui_metric(36))
            : 1.0F;
        const auto delta_for_wheel = [per_event_step](const float value) {
            if (!per_event_step || value == 0.0F) return value;
            return std::copysign(1.0F, value);
        };
        return scroll_view_content(scroll, {
            scroll->scroll_offset.x + delta_for_wheel(delta.x) * unit,
            scroll->scroll_offset.y + delta_for_wheel(delta.y) * unit});
    }
    if (const auto window = std::dynamic_pointer_cast<Window>(target)) {
        const float unit = wheel ? scaled_ui_metric(36) : 1;
        return scroll_window_content(window, {window->content_offset.x + delta.x * unit,
                                              window->content_offset.y + delta.y * unit});
    }
    if (const auto edit = std::dynamic_pointer_cast<TextEdit>(target);
        edit && edit->multiline && text) {
        const auto result=resolve_text_scroll(edit,text,{delta.x,delta.y,true,wheel});
        if (invalidated) *invalidated=result.invalidated;
        return result.moved;
    }
    return false;
}

bool UiScrollStateMachine::wheel_in_scope(const WidgetPtr& scope, const WidgetPtr& target,
    Point delta, Modifiers modifiers, float row_height, const ScrollTextProvider& text) {
    if ((static_cast<std::uint32_t>(modifiers) & static_cast<std::uint32_t>(Modifiers::Shift)) != 0)
        delta = {delta.x + delta.y, 0};
    return apply_in_scope(scope, target, {-delta.x, -delta.y}, true, row_height, text);
}

bool UiScrollStateMachine::pan_in_scope(const WidgetPtr& scope, const WidgetPtr& target,
    Point displacement, float row_height, const ScrollTextProvider& text) {
    return apply_in_scope(scope, target, {-displacement.x, -displacement.y}, false, row_height, text);
}

bool UiScrollStateMachine::apply_in_scope(const WidgetPtr& scope, const WidgetPtr& target,
    Point delta, bool wheel, float row_height, const ScrollTextProvider& text) {
    if (!std::isfinite(delta.x) || !std::isfinite(delta.y) ||
        !std::isfinite(row_height) || row_height <= 0) return false;
    const auto path = input_scope_path(scope, target);
    bool moved = false;
    for (const Point axis : {Point{delta.x, 0}, Point{0, delta.y}}) {
        if (axis.x == 0 && axis.y == 0) continue;
        for (auto it = path.rbegin(); it != path.rend(); ++it) {
            // 配置・文字計測のコールバックが、対象を切り離したり親を変えたりする場合がある。
            if (input_scope_path(scope, target) != path) return moved;
            const auto& view = *it;
            const auto list = std::dynamic_pointer_cast<ListView>(view);
            const auto table = std::dynamic_pointer_cast<TableView>(view);
            const auto tree = std::dynamic_pointer_cast<TreeView>(view);
            bool retains_fraction = false;
            if (!wheel && axis.y != 0 && (list || table || tree)) {
                const auto geometry = item_scroll_geometry(*view, row_height);
                const auto top = list ? list->top_index : table ? table->top_index : tree->top_index;
                retains_fraction = axis.y > 0 ? top < geometry.vertical.maximum : top > 0;
            }
            bool invalidated=false;
            if (apply(view, axis, wheel, row_height, text,&invalidated)) { moved = true; break; }
            if (invalidated) return moved;
            if (retains_fraction) break;
        }
    }
    return moved;
}

bool wheel_combo_selection(const std::shared_ptr<ComboBox>& combo, float delta) {
    if (!combo || !combo->visible() || !combo->enabled() || combo->items.empty() ||
        !std::isfinite(delta) || delta == 0) return false;
    const int direction = delta > 0 ? -1 : 1;
    const auto target = combo->selected_index
        ? adjacent_combo_item(*combo,*combo->selected_index,direction)
        : first_enabled_combo_item(*combo,direction > 0 ? 0U : combo->items.size()-1);
    if (target) (void)select_combo_item(*combo,*target);
    return true;
}

} // namespace cwxeditor::lib
