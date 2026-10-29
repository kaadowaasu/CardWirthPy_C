#pragma once

#include "cw/lib/compat.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <span>
#include <utility>

namespace cw::render {

// フレーム内の更新矩形はネイティブ値で保持する。互換 Object
// ラッパーは CWPy の API 境界だけで受け入れ、フレー
// ムの蓄積・処理時に矩形ごとのヒープオブジェクトを確保しない。
class DirtyRegion {
public:
    explicit DirtyRegion(std::size_t arg_max_rects = 64U)
        : m_max_rects(std::clamp(arg_max_rects, std::size_t{1U}, kCapacity)) {}

    void add(::cw::lib::Rect arg_rect)
    {
        if (m_full || empty_rect(arg_rect)) return;

        // LayeredDirty の既存の動作を保ち、重なる矩
        // 形だけでなく接する矩形も結合する。広がった矩形が以前の
        // 項目に接する場合があるため、結合後は走査をやり直す。
        for (std::size_t index = 0; index < m_size;) {
            if (overlaps_or_touches(arg_rect, m_rects[index])) {
                arg_rect = unite(arg_rect, m_rects[index]);
                std::move(m_rects.begin() + static_cast<std::ptrdiff_t>(index + 1U),
                          m_rects.begin() + static_cast<std::ptrdiff_t>(m_size),
                          m_rects.begin() + static_cast<std::ptrdiff_t>(index));
                --m_size;
                index = 0;
            } else {
                ++index;
            }
        }
        if (m_size < m_max_rects) {
            m_rects[m_size++] = arg_rect;
            return;
        }

        // 画素を落とさず、通知が集中した場合の処理量を制限する。離れた
        // 矩形が 65 個になったら一つの外接矩形にまとめ、vector
        // を確保せずに以前の CWPy の上限と同じ動作にする。
        auto combined = arg_rect;
        for (std::size_t index = 0; index < m_size; ++index)
            combined = unite(combined, m_rects[index]);
        m_rects[0] = combined;
        m_size = 1U;
    }

    void add_full()
    {
        m_full = true;
        m_size = 0U;
    }

    void clear()
    {
        m_full = false;
        m_size = 0U;
    }

    [[nodiscard]] bool full() const noexcept { return m_full; }
    [[nodiscard]] bool empty() const noexcept { return !m_full && m_size == 0U; }
    [[nodiscard]] std::size_t size() const noexcept { return m_size; }
    [[nodiscard]] std::span<const ::cw::lib::Rect> rects() const noexcept
    {
        return {m_rects.data(), m_size};
    }

    [[nodiscard]] ::cw::lib::Rect bounds() const noexcept
    {
        if (m_size == 0U) return {};
        auto result = m_rects.front();
        for (std::size_t index = 1; index < m_size; ++index)
            result = unite(result, m_rects[index]);
        return result;
    }

    [[nodiscard]] DirtyRegion take()
    {
        DirtyRegion result(m_max_rects);
        result.m_full = std::exchange(m_full, false);
        result.m_size = std::exchange(m_size, 0U);
        std::copy_n(m_rects.begin(), result.m_size, result.m_rects.begin());
        return result;
    }

private:
    static bool empty_rect(const ::cw::lib::Rect& arg_rect) noexcept
    {
        return arg_rect.width <= 0 || arg_rect.height <= 0;
    }

    static bool overlaps_or_touches(const ::cw::lib::Rect& arg_left,
                                    const ::cw::lib::Rect& arg_right) noexcept
    {
        return arg_left.x <= saturated_end(arg_right.x, arg_right.width) &&
               arg_right.x <= saturated_end(arg_left.x, arg_left.width) &&
               arg_left.y <= saturated_end(arg_right.y, arg_right.height) &&
               arg_right.y <= saturated_end(arg_left.y, arg_left.height);
    }

    static ::cw::lib::Rect unite(const ::cw::lib::Rect& arg_left,
                                 const ::cw::lib::Rect& arg_right) noexcept
    {
        if (empty_rect(arg_left)) return arg_right;
        if (empty_rect(arg_right)) return arg_left;
        const auto left = std::min(arg_left.x, arg_right.x);
        const auto top = std::min(arg_left.y, arg_right.y);
        const auto right = std::max(saturated_end(arg_left.x, arg_left.width),
                                    saturated_end(arg_right.x, arg_right.width));
        const auto bottom = std::max(saturated_end(arg_left.y, arg_left.height),
                                     saturated_end(arg_right.y, arg_right.height));
        return {left, top, saturated_span(left, right), saturated_span(top, bottom)};
    }

    static ::cw::lib::Integer saturated_end(const ::cw::lib::Integer origin,
                                             const ::cw::lib::Integer extent) noexcept
    {
        constexpr auto maximum = (std::numeric_limits<::cw::lib::Integer>::max)();
        return extent > 0 && origin > maximum - extent ? maximum : origin + extent;
    }

    static ::cw::lib::Integer saturated_span(const ::cw::lib::Integer low,
                                              const ::cw::lib::Integer high) noexcept
    {
        constexpr auto maximum = (std::numeric_limits<::cw::lib::Integer>::max)();
        if (high <= low) return 0;
        if (low < 0 && high > maximum + low) return maximum;
        return high - low;
    }

    static constexpr std::size_t kCapacity = 64U;
    std::array<::cw::lib::Rect, kCapacity> m_rects{};
    std::size_t m_size{};
    std::size_t m_max_rects{64U};
    bool m_full{};
};

}  // namespace cw::render
