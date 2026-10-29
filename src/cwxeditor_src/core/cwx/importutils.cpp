#include "cwxeditor_src/core/cwx/importutils.hpp"

namespace cwxeditor::cwx::importutils {

String ImportFlag::name() const {
    return flag ? flag->name() : String{};
}

String ImportStep::name() const {
    return step ? step->name() : String{};
}

String ImportVariant::name() const {
    return variant ? variant->name() : String{};
}

}  // namespace cwxeditor::cwx::importutils
