#pragma once

// RGBA port of pygame 1.9.6 draw.c (Copyright 2000-2001 Pete Shinners).
// SPDX-License-Identifier: LGPL-2.0-or-later
// Upstream source: https://pypi.org/project/pygame/1.9.6/
// This library is free software under the GNU Library General Public License,
// version 2 or later, without any warranty. The upstream notice and source
// provenance are retained in the accompanying pygame_draw.LICENSE file.
// Keep scan-line integer division, float Wu coverage and channel truncation:
// replacing them with a different rasterizer changes the original Py pixels.

#include "cw/lib/compat.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace cw::lib::pygame_draw {

inline bool valid(const Surface& surface) {
    return surface.size.width > 0 && surface.size.height > 0 &&
        static_cast<std::uint64_t>(surface.size.width) <= surface.rgba.size() / 4 /
            static_cast<std::uint64_t>(surface.size.height);
}

inline void pixel(Surface& surface, Integer x, Integer y, Color color, float coverage = 1.0f) {
    if (!valid(surface) || x < 0 || y < 0 || x >= surface.size.width || y >= surface.size.height) return;
    auto* dest = surface.rgba.data() + (y * surface.size.width + x) * 4;
    const std::array<std::uint8_t,4> channels{color.r,color.g,color.b,color.a};
    for (std::size_t i = 0; i < 4; ++i)
        dest[i] = static_cast<std::uint8_t>(coverage * channels[i] + (1.0f - coverage) * dest[i]);
}

inline void filled_polygon(Surface& surface, const std::vector<Point>& points, Color color) {
    if (!valid(surface) || points.size() < 3) return;
    auto min_y = points.front().y, max_y = min_y;
    for (const auto& p : points) { min_y = std::min(min_y,p.y); max_y = std::max(max_y,p.y); }
    const auto span = [&](Integer x1, Integer x2, Integer y) {
        if (x1 > x2) std::swap(x1,x2);
        x1 = std::max<Integer>(0,x1); x2 = std::min(surface.size.width-1,x2);
        for (auto x = x1; x <= x2; ++x) pixel(surface,x,y,color);
    };
    if (min_y == max_y) {
        auto min_x = points.front().x, max_x = min_x;
        for (const auto& p : points) { min_x = std::min(min_x,p.x); max_x = std::max(max_x,p.x); }
        span(min_x,max_x,min_y); return;
    }
    std::vector<Integer> intersections;
    intersections.reserve(points.size());
    for (auto y = std::max<Integer>(0,min_y); y <= std::min(surface.size.height-1,max_y); ++y) {
        intersections.clear();
        for (std::size_t i = 0; i < points.size(); ++i) {
            auto a = points[i ? i-1 : points.size()-1], b = points[i];
            if (a.y == b.y) continue;
            if (a.y > b.y) std::swap(a,b);
            if ((y >= a.y && y < b.y) || (y == max_y && b.y == max_y))
                intersections.push_back((y-a.y)*(b.x-a.x)/(b.y-a.y)+a.x);
        }
        std::sort(intersections.begin(),intersections.end());
        for (std::size_t i = 0; i+1 < intersections.size(); i += 2)
            span(intersections[i],intersections[i+1],y);
    }
    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto& a = points[i ? i-1 : points.size()-1]; const auto& b = points[i];
        if (min_y < b.y && b.y < max_y && a.y == b.y) span(a.x,b.x,b.y);
    }
}

inline void aaline(Surface& surface, Point from, Point to, Color color) {
    if (!valid(surface)) return;
    float x1 = static_cast<float>(from.x), y1 = static_cast<float>(from.y);
    float x2 = static_cast<float>(to.x), y2 = static_cast<float>(to.y);
    const auto right = static_cast<float>(surface.size.width-1);
    const auto bottom = static_cast<float>(surface.size.height-1);
    const auto code = [&](float x,float y) { return (x<0?1:0)|(x>right?2:0)|(y>bottom?4:0)|(y<0?8:0); };
    for (;;) {
        const auto c1 = code(x1,y1), c2 = code(x2,y2);
        if (!(c1|c2)) break;
        if (c1&c2) return;
        if (!c1) { std::swap(x1,x2); std::swap(y1,y2); continue; }
        const float slope = x2 != x1 ? (y2-y1)/(x2-x1) : 1.0f;
        if (c1&1) { y1 += -x1*slope; x1 = 0; }
        else if (c1&2) { y1 += (right-x1)*slope; x1 = right; }
        else if (c1&4) { if (x2!=x1) x1 += (bottom-y1)/slope; y1 = bottom; }
        else { if (x2!=x1) x1 += -y1/slope; y1 = 0; }
    }
    const auto fraction = [](float v) { return v-std::trunc(v); };
    float dx = x2-x1, dy = y2-y1;
    if (dx == 0 && dy == 0) { pixel(surface,static_cast<Integer>(x1),static_cast<Integer>(y1),color); return; }
    // 転置でも、水平・垂直の Wu の規則を厳密に共有する。
    const bool transpose = std::fabs(dx) <= std::fabs(dy);
    if (transpose) { std::swap(x1,y1); std::swap(x2,y2); std::swap(dx,dy); }
    if (x1 > x2) { std::swap(x1,x2); std::swap(y1,y2); dx = -dx; dy = -dy; }
    const float slope = dy/dx;
    const auto pair = [&](Integer x,float y,float gap) {
        const auto base = static_cast<Integer>(y);
        const auto paint = [&](Integer px,Integer py,float coverage) {
            if (transpose) pixel(surface,py,px,color,coverage); else pixel(surface,px,py,color,coverage);
        };
        paint(x,base,(1.0f-fraction(y))*gap);
        paint(x,base+1,fraction(y)*gap);
    };
    const float start = std::trunc(x1)+0.5f, end = std::trunc(x2)+0.5f;
    const float start_y = y1+slope*(start-x1), end_y = y2+slope*(end-x2);
    const auto first = static_cast<Integer>(start), last = static_cast<Integer>(end);
    pair(first,start_y,1.0f-fraction(x1));
    pair(last,end_y,1.0f-fraction(x2));
    float y = start_y+slope;
    for (auto x = first+1; x < last; ++x) { pair(x,y,1.0f); y += slope; }
}

inline void aalines(Surface& surface, const std::vector<Point>& points, Color color, bool closed = false) {
    for (std::size_t i = 1; i < points.size(); ++i) aaline(surface,points[i-1],points[i],color);
    if (closed && points.size() > 2) aaline(surface,points.back(),points.front(),color);
}

} // namespace cw::lib::pygame_draw
