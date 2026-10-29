#pragma once
#include "cw/lib/compat.hpp"
#include <algorithm>
#include <limits>

namespace cw::lib::money {
// 意図的なネイティブ拡張。残高更新の上限は 999,999,999 で
// 、Python の 9,999,999 より大きい。Python の
// 上限を超える範囲で差が出る。旧バイナリー出力の制限は形式ごとに保つ。
inline constexpr Integer maximum = 999999999;
inline constexpr std::size_t digits = 9;
constexpr Integer add(Integer value, Integer delta) noexcept {
    // 元の残高が範囲外でも、差分を適用してから上限・下限に補正する。ネイティブの
    // 格納型のオーバーフローだけを防ぐ。Python の整数加算に上限はない。
    if (delta > 0 && value > (std::numeric_limits<Integer>::max)() - delta) return maximum;
    if (delta < 0 && value < (std::numeric_limits<Integer>::min)() - delta) return 0;
    return std::clamp<Integer>(value + delta, 0, maximum);
}
}
