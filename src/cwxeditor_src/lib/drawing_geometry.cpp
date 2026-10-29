#include "drawing_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>
#include <stdexcept>

namespace cwxeditor::lib {
namespace {
constexpr double epsilon = 1e-7;
bool finite(Point p) { return std::isfinite(p.x) && std::isfinite(p.y); }
bool same(Point a, Point b) { return a.x == b.x && a.y == b.y; }
double cross(Point a, Point b, Point c) {
    return (double(b.x)-a.x)*(double(c.y)-a.y) - (double(b.y)-a.y)*(double(c.x)-a.x);
}
std::vector<Point> clean(std::span<const Point> points) {
    std::vector<Point> result;
    for (const auto p : points) {
        if (!finite(p)) return {};
        if (result.empty() || !same(result.back(), p)) result.push_back(p);
    }
    return result;
}
bool valid(Rect r) {
    return finite({r.x,r.y}) && std::isfinite(r.width) && std::isfinite(r.height) &&
           r.width > 0 && r.height > 0 && finite({r.x+r.width,r.y+r.height});
}
struct Edge {
    Point a, b;
    double x(double y) const { return a.x+(y-a.y)*(double(b.x)-a.x)/(double(b.y)-a.y); }
    int direction() const { return b.y > a.y ? 1 : -1; }
};
void triangle(DrawingGeometry& geometry, Point a, Point b, Point c) {
    if (finite(a) && finite(b) && finite(c) && std::abs(cross(a,b,c)) > epsilon)
        geometry.triangles.push_back({a,b,c});
}
void wide_path(DrawingGeometry& geometry, const std::vector<Point>& path, float width) {
    const bool closed = path.size() > 2 && same(path.front(),path.back());
    const auto count = path.size() - (closed ? 1U : 0U);
    if (count < 2) return;
    const float half = width*.5F;
    std::vector<Point> normals;
    for (std::size_t i=0; i+1<path.size(); ++i) {
        const double dx=double(path[i+1].x)-path[i].x, dy=double(path[i+1].y)-path[i].y;
        const double length=std::hypot(dx,dy);
        normals.push_back({float(-dy/length),float(dx/length)});
    }
    std::vector<Point> left, right;
    for (std::size_t i=0; i<count; ++i) {
        Point offset{};
        if (!closed && i==0) offset={normals.front().x*half,normals.front().y*half};
        else if (!closed && i==count-1) offset={normals.back().x*half,normals.back().y*half};
        else {
            const auto previous=normals[(i+normals.size()-1)%normals.size()];
            const auto next=normals[i%normals.size()];
            const double denominator=1.0+double(previous.x)*next.x+double(previous.y)*next.y;
            if (denominator > epsilon) {
                offset={float((previous.x+next.x)*half/denominator),
                        float((previous.y+next.y)*half/denominator)};
                const double length=std::hypot(offset.x,offset.y);
                if (length>4*half) { offset.x*=float(4*half/length); offset.y*=float(4*half/length); }
            } else offset={next.x*half,next.y*half};
        }
        left.push_back({path[i].x+offset.x,path[i].y+offset.y});
        right.push_back({path[i].x-offset.x,path[i].y-offset.y});
    }
    // 内側の閉じた輪郭も含む一つの巻き数による輪郭で描く。 隣り合う線
    // 分の四角形が重なる箇所で、アルファが重ねて合成されるのを防ぐ。
    std::vector<Point> outline=left;
    if (closed) { outline.push_back(left.front()); outline.push_back(right.front()); }
    outline.insert(outline.end(),right.rbegin(),right.rend());
    auto fill=polygon_geometry(outline,FillRule::Winding);
    geometry.triangles.insert(geometry.triangles.end(),fill.triangles.begin(),fill.triangles.end());
}
} // namespace

DrawingGeometry polygon_geometry(std::span<const Point> input, FillRule rule) {
    DrawingGeometry result;
    const auto points=clean(input);
    if (points.size()<3) return result;
    std::vector<Edge> edges;
    std::vector<double> levels;
    for (std::size_t i=0;i<points.size();++i) {
        const auto a=points[i], b=points[(i+1)%points.size()];
        levels.push_back(a.y);
        if (a.y!=b.y) edges.push_back({a,b});
    }
    // 交差点でも分割し、凹形状・自己交差の輪郭を含め、各帯の中で
    // 順序を固定する。 三角形ファンで表せることを前提にしない。
    for (std::size_t i=0;i<edges.size();++i) for (std::size_t j=i+1;j<edges.size();++j) {
        const auto& a=edges[i]; const auto& b=edges[j];
        const double low=std::max(std::min(a.a.y,a.b.y),std::min(b.a.y,b.b.y));
        const double high=std::min(std::max(a.a.y,a.b.y),std::max(b.a.y,b.b.y));
        if (high<=low) continue;
        const double dl=a.x(low)-b.x(low), dh=a.x(high)-b.x(high);
        if ((dl<0 && dh>0) || (dl>0 && dh<0)) levels.push_back(low+(high-low)*dl/(dl-dh));
    }
    std::sort(levels.begin(),levels.end());
    levels.erase(std::unique(levels.begin(),levels.end()),levels.end());
    struct Crossing { const Edge* edge; double x; };
    std::vector<Crossing> active;
    for (std::size_t i=1;i<levels.size();++i) {
        const double low=levels[i-1], high=levels[i], middle=(low+high)*.5;
        if (high-low<=epsilon) continue;
        active.clear();
        for (const auto& edge:edges)
            if (middle>std::min(edge.a.y,edge.b.y) && middle<std::max(edge.a.y,edge.b.y))
                active.push_back({&edge,edge.x(middle)});
        std::sort(active.begin(),active.end(),[](auto a,auto b){return a.x<b.x;});
        int winding=0; bool parity=false;
        const Edge* left=nullptr;
        for (const auto& crossing:active) {
            const bool before=rule==FillRule::EvenOdd ? parity : winding!=0;
            parity=!parity; winding+=crossing.edge->direction();
            const bool after=rule==FillRule::EvenOdd ? parity : winding!=0;
            if (!before && after) left=crossing.edge;
            if (before && !after && left) {
                const auto* right=crossing.edge;
                const Point a{float(left->x(low)),float(low)},b{float(right->x(low)),float(low)},
                            c{float(right->x(high)),float(high)},d{float(left->x(high)),float(high)};
                triangle(result,a,b,c); triangle(result,a,c,d);
                left=nullptr;
            }
        }
    }
    return result;
}

DrawingGeometry stroke_path(std::span<const Point> input, bool closed, float width, StrokeStyle style) {
    DrawingGeometry result;
    if (!std::isfinite(width) || width<0) return result;
    if (width==0) width=1;
    auto points=clean(input);
    if (closed && points.size()>1 && same(points.front(),points.back())) points.pop_back();
    if (points.size()<2) return result;
    if (points.size()==2) closed=false;
    if (closed && !same(points.front(),points.back())) points.push_back(points.front());
    std::vector<std::vector<Point>> runs;
    if (style==StrokeStyle::Solid) runs.push_back(points);
    else {
        // 破線の長さはペン幅の単位で表し、すべてのOSで共有する。
        const double unit=std::max(1.0F,width);
        std::vector<double> pattern;
        switch (style) {
        case StrokeStyle::Dash: pattern={3*unit,unit}; break;
        case StrokeStyle::Dot: pattern={unit,unit}; break;
        case StrokeStyle::DashDot: pattern={3*unit,unit,unit,unit}; break;
        default: break;
        }
        if (pattern.empty()) return result;
        double length=0;
        for(std::size_t i=1;i<points.size();++i)
            length+=std::hypot(double(points[i].x)-points[i-1].x,double(points[i].y)-points[i-1].y);
        const double cycle=std::accumulate(pattern.begin(),pattern.end(),0.0);
        // 不正な指定座標で何十億もの破線を展開し、イベントスレ
        // ッドを止めない。 形状を黙って捨てずにフレームを失敗
        // とし、一括反映する表示側が前のフレームを保持する。
        if (length/cycle*(pattern.size()/2)>65536)
            throw std::length_error("Canvas dash geometry exceeds frame budget");
        std::size_t phase=0;
        double remaining=pattern.front();
        std::vector<Point> run;
        for (std::size_t i=1;i<points.size();++i) {
            const auto a=points[i-1],b=points[i];
            const double dx=double(b.x)-a.x,dy=double(b.y)-a.y,length=std::hypot(dx,dy);
            double cursor=0;
            while (cursor<length-epsilon) {
                const double advance=std::min(remaining,length-cursor);
                const auto at=[&](double distance){return Point{float(a.x+dx*distance/length),float(a.y+dy*distance/length)};};
                if (phase%2==0) {
                    if (run.empty()) run.push_back(at(cursor));
                    run.push_back(at(cursor+advance));
                }
                cursor+=advance; remaining-=advance;
                if (remaining<=epsilon) {
                    if (!run.empty()) { runs.push_back(std::move(run)); run.clear(); }
                    phase=(phase+1)%pattern.size(); remaining=pattern[phase];
                }
            }
        }
        if (!run.empty()) runs.push_back(std::move(run));
        if (closed && runs.size()>1 && same(runs.front().front(),points.front()) &&
            same(runs.back().back(),points.front())) {
            auto joined=std::move(runs.back()); runs.pop_back();
            joined.insert(joined.end(),runs.front().begin()+1,runs.front().end());
            runs.front()=std::move(joined);
        }
    }
    for (auto run:runs) {
        run=clean(run);
        if (width<=1) {
            // ネイティブの一画素線は終点も含む。 破線の各区間は半開
            // 区間とし、一画素の隙間・点が実線にならないようにする。
            if (style!=StrokeStyle::Solid) {
                double trim=1;
                while (run.size()>1 && trim>epsilon) {
                    const auto a=run[run.size()-2],b=run.back();
                    const double length=std::hypot(double(b.x)-a.x,double(b.y)-a.y);
                    if (length>trim) {
                        run.back()={float(b.x+(a.x-b.x)*trim/length),float(b.y+(a.y-b.y)*trim/length)};
                        break;
                    }
                    trim-=length; run.pop_back();
                }
            }
            result.lines.push_back(std::move(run));
        }
        else wide_path(result,run,width);
    }
    return result;
}

std::vector<Point> rectangle_path(Rect rect) {
    if (!valid(rect)) return {};
    // 従来のネイティブ矩形と同じ、端点を含む一画素の輪郭に合わせる。
    const float right=rect.x+std::max(0.0F,rect.width-1),bottom=rect.y+std::max(0.0F,rect.height-1);
    return {{rect.x,rect.y},{right,rect.y},{right,bottom},{rect.x,bottom}};
}
std::vector<Point> ellipse_path(Rect rect) {
    if (!valid(rect)) return {};
    constexpr unsigned segments=64;
    std::vector<Point> result;
    result.reserve(segments);
    for (unsigned i=0;i<segments;++i) {
        const double angle=i*2*std::numbers::pi/segments;
        result.push_back({float(rect.x+rect.width*.5+std::cos(angle)*rect.width*.5),
                          float(rect.y+rect.height*.5+std::sin(angle)*rect.height*.5)});
    }
    return result;
}
} // namespace cwxeditor::lib
