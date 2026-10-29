#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cw::skill_distribution {
inline double rate(std::int64_t level_capacity, std::int64_t owned_skills) {
    level_capacity = std::max<std::int64_t>(1, level_capacity);
    return double(level_capacity) / double(std::max(level_capacity, owned_skills));
}
inline std::int64_t weight(std::int64_t remaining_skills, double coefficient) {
    return std::llround(double(std::max<std::int64_t>(0, remaining_skills)) *
                        std::clamp(coefficient, 0.0, 1.0));
}
} // namespace cw::skill_distribution
