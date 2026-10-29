#include "cwxeditor_src/gui/cwx/editor/gui/dwt/centerlayout.hpp"
#include "cwxeditor_src/lib/ui_layout.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::centerlayout {
namespace {

std::optional<lib::CenterLayoutAnchors> common_anchors(const LayoutData* data) {
    const auto* edges = data ? std::get_if<Insets>(data) : nullptr;
    if (!edges) return {};
    const auto optional = [](const std::int32_t value) -> std::optional<float> {
        return value == Insets::default_value ? std::nullopt
                                               : std::optional<float>{static_cast<float>(value)};
    };
    return lib::CenterLayoutAnchors{optional(edges->n), optional(edges->e),
                                    optional(edges->s), optional(edges->w)};
}

} // namespace

Insets Insets::from(const lib::Insets& value) noexcept {
    return {static_cast<std::int32_t>(value.top),
            static_cast<std::int32_t>(value.right),
            static_cast<std::int32_t>(value.bottom),
            static_cast<std::int32_t>(value.left)};
}

lib::Insets Insets::to_lib() const noexcept {
    return {static_cast<float>(n), static_cast<float>(e),
            static_cast<float>(s), static_cast<float>(w)};
}

CenterLayout::CenterLayout(const Style style, const std::int32_t margin,
                           MeasureCallback measure)
    : margin_(margin), style_(style), measure_(std::move(measure)) {
    // 標準LayoutのメタデータをDの余白と同期する。下
    // 記の配置式は意図的にmargin_を使い、汎用メタ
    // データの変更でD互換の動作が変わらないようにする。
    padding = {static_cast<float>(margin_), static_cast<float>(margin_),
               static_cast<float>(margin_), static_cast<float>(margin_)};
}

void CenterLayout::set_measure_callback(MeasureCallback measure) {
    measure_ = std::move(measure);
}

void CenterLayout::set_layout_data(const lib::Widget& child, LayoutData data) {
    layout_data_[&child] = std::move(data);
}

void CenterLayout::clear_layout_data(const lib::Widget& child) noexcept {
    layout_data_.erase(&child);
}

const LayoutData* CenterLayout::layout_data(
    const lib::Widget& child) const noexcept {
    const auto it = layout_data_.find(&child);
    return it == layout_data_.end() ? nullptr : &it->second;
}

lib::Size CenterLayout::measured_size(const lib::Widget& child) const {
    if (measure_) return measure_(child);
    const auto& item = child.layout_item();
    const auto bounds = child.bounds();
    return lib::resolve_center_layout_preferred_size(item.preferred, item.minimum,
                                                       {bounds.width, bounds.height});
}

lib::Size CenterLayout::child_size(const lib::Widget& child) const {
    if (const auto* data = layout_data(child)) {
        if (const auto* point = std::get_if<lib::Point>(data)) {
            return {point->x, point->y};
        }
        if (const auto* size = std::get_if<lib::Size>(data)) return *size;
    }
    return measured_size(child);
}

lib::Size CenterLayout::compute_size(const lib::Container& composite,
                                     float width_hint, float height_hint,
                                     const bool /*flush_cache*/) const {
    std::vector<lib::Size> children;
    children.reserve(composite.children().size());
    for (const auto& child : composite.children()) {
        if (child) children.push_back(child_size(*child));
    }
    return lib::measure_center_layout(children, static_cast<float>(margin_),
                                      width_hint, height_hint);
}

void CenterLayout::layout(lib::Container& composite,
                          const bool flush_cache) const {
    // Widgetの矩形は親の座標系で表す。子の配置はSWTのComposite::getClientArea()
    // と同様に、コンテナー内の原点から始
    // め、利用可能な幅と高さだけを使う。
    const auto bounds = composite.bounds();
    layout(composite, {0.0F, 0.0F, bounds.width, bounds.height}, flush_cache);
}

void CenterLayout::layout(lib::Container& composite,
                           const lib::Rect& client_area,
                           const bool /*flush_cache*/) const {
    for (const auto& child_owner : composite.children()) {
        if (!child_owner) continue;
        auto& child = *child_owner;
        const LayoutData* data = layout_data(child);
        const auto* child_data =
            data ? std::get_if<CenterLayoutData>(data) : nullptr;
        lib::CenterLayoutItemRequest request;
        request.client = client_area;
        request.preferred = child_size(child);
        request.margin = static_cast<float>(margin_);
        request.center_horizontal = (style_ & Horizontal) != 0U;
        request.center_vertical = (style_ & Vertical) != 0U;
        request.fill_horizontal = fillHorizontal ||
            (child_data && child_data->fillHorizontal);
        request.fill_vertical = fillVertical ||
            (child_data && child_data->fillVertical);
        request.anchors = common_anchors(data);
        if (const auto* rectangle = data ? std::get_if<lib::Rect>(data) : nullptr)
            request.explicit_bounds = *rectangle;
        child.set_bounds(lib::plan_center_layout_item(request));
    }
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::centerlayout
