#include "cwxeditor_src/gui/cwx/editor/gui/dwt/areawindow.hpp"

namespace cwxeditor::cwx::editor::gui::dwt::areawindow {

std::uint64_t MenuActionRegistry::put(types::MenuID id, lib::Action action,
                             std::function<bool()> enabled,
                             std::function<bool()> checked) {
    const auto generation=next_generation_++;
    entries_[id] = Entry{std::move(action), std::move(enabled),
                         std::move(checked), generation};
    return generation;
}

void MenuActionRegistry::remove(types::MenuID id,const std::uint64_t expected_generation){
    const auto found=entries_.find(id);
    if(found!=entries_.end() && found->second.generation==expected_generation)entries_.erase(found);
}

void MenuActionRegistry::remove(types::MenuID id) {
    entries_.erase(id);
}

bool MenuActionRegistry::dispatch(types::MenuID id) {
    const auto found = entries_.find(id);
    if (found == entries_.end()) return false;
    const auto entry=found->second;
    const std::weak_ptr<void> life=lifetime_;
    if (entry.enabled && !entry.enabled()) return false;
    if(life.expired())return false;
    const auto current=entries_.find(id);
    if(current==entries_.end() || current->second.generation!=entry.generation)return false;
    if (!entry.action) return false;
    entry.action();
    return true;
}

bool MenuActionRegistry::enabled(types::MenuID id) const {
    const auto found = entries_.find(id);
    if (found == entries_.end()) return false;
    const auto entry=found->second;
    const std::weak_ptr<void> life=lifetime_;
    const bool enabled=!entry.enabled || entry.enabled();
    if(life.expired())return false;
    const auto current=entries_.find(id);
    return enabled && current!=entries_.end() && current->second.generation==entry.generation;
}

bool MenuActionRegistry::checked(types::MenuID id) const {
    const auto found = entries_.find(id);
    if (found == entries_.end()) return false;
    const auto entry = found->second;
    const std::weak_ptr<void> life = lifetime_;
    const bool checked = entry.checked && entry.checked();
    if (life.expired()) return false;
    const auto current = entries_.find(id);
    return checked && current != entries_.end() &&
           current->second.generation == entry.generation;
}

}  // namespace cwxeditor::cwx::editor::gui::dwt::areawindow
