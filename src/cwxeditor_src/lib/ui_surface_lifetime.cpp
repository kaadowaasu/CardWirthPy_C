#include "ui_surface_lifetime.hpp"
#include <algorithm>
#include <unordered_set>

namespace cwxeditor::lib {
std::vector<std::uint64_t> plan_surface_release(
    std::span<const SurfaceLifetimeEntry> entries,
    std::span<const std::uintptr_t> busy_surfaces) {
    std::unordered_set<std::uint64_t> generations;
    for(const auto& entry:entries)
        if(!entry.generation || !generations.insert(entry.generation).second) return {};
    std::vector<bool> removed(entries.size());
    std::vector<std::uint64_t> result;
    for(;;) {
        auto next=entries.size();
        for(std::size_t i=0;i<entries.size();++i) {
            const auto& candidate=entries[i];
            if(removed[i] || !candidate.retired ||
               std::find(busy_surfaces.begin(),busy_surfaces.end(),candidate.surface)!=busy_surfaces.end()) continue;
            bool blocked=candidate.parent==candidate.generation;
            for(std::size_t j=0;!blocked && j<entries.size();++j) {
                if(i==j || removed[j]) continue;
                const auto& dependent=entries[j];
                blocked=dependent.parent==candidate.generation ||
                    (candidate.owns_surface && candidate.surface && dependent.surface==candidate.surface);
            }
            if(!blocked && (next==entries.size() || candidate.generation<entries[next].generation)) next=i;
        }
        if(next==entries.size()) break;
        removed[next]=true;
        result.push_back(entries[next].generation);
    }
    return result;
}
} // namespace cwxeditor::lib
