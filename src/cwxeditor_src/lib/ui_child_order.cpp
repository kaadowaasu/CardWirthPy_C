#include "ui_child_order.hpp"
#include <algorithm>

namespace cwxeditor::lib {
std::vector<WidgetPtr> presentation_children(const Container& owner) {
    std::vector<WidgetPtr> children;
    children.reserve(owner.children().size());
    for(const auto& child:owner.children())
        if(child && child->visible() && !std::dynamic_pointer_cast<Window>(child))
            children.push_back(child);
    std::stable_partition(children.begin(),children.end(),[](const WidgetPtr& child) {
        const auto canvas=std::dynamic_pointer_cast<Canvas>(child);
        return canvas && canvas->behind_siblings;
    });
    return children;
}
} // namespace cwxeditor::lib
