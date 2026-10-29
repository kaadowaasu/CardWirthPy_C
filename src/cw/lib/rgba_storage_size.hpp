#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace cw::lib {

// Python の Surface の確保は、画素領域を作る前に表現不能な
// 寸法を拒否する。ネイティブの vector 画像も同じ検査境界を使い、確
// 保検査前に符号付き寸法を乗算したり size_t を循環させたりしない。
inline std::size_t checked_rgba_byte_count(std::uint64_t width,
                                          std::uint64_t height,
                                          std::size_t allocation_limit) {
    if (width > allocation_limit / 4U ||
        (width && height > allocation_limit / (width * 4U)))
        throw std::length_error("dimensions exceed RGBA allocation limit");
    return static_cast<std::size_t>(width * height * 4U);
}

} // namespace cw::lib
