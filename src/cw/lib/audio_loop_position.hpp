#pragma once
#include "cw/lib/compat.hpp"
#include "cw/lib/python_float.hpp"
#include <limits>

namespace cw::lib {
inline Integer audio_loop_byte_position(double samples, double samples_per_byte, Integer samples_to_bytes)
{
    const auto position = python_float_floor_divide(samples, samples_per_byte);
    // INT64_MAX は double では上へ丸められるため、含まない上限と比較する。
    if (!std::isfinite(position) || position < 0 || position >= std::ldexp(1.0, 63) ||
        samples_to_bytes < 0)
        throw std::overflow_error("audio loop position is outside the supported byte range");
    const auto integer = static_cast<Integer>(position);
    if (samples_to_bytes && integer > std::numeric_limits<Integer>::max() / samples_to_bytes)
        throw std::overflow_error("audio loop byte position overflow");
    return integer * samples_to_bytes;
}
}
