#pragma once

#include "graphics.hpp"
#include <array>

namespace cwxeditor::lib {

// OS・描画実装・EditorのUI倍率に依存しない、指定されたCanvas座
// 標。 細い線はネイティブの1px描画を保ち、太い線と塗りはメッシュで表す。
struct DrawingGeometry {
    std::vector<std::vector<Point>> lines;
    std::vector<std::array<Point, 3>> triangles;
};

// 端は平らに切り、接合のマイター長を制限し、破線の位相は頂点をまたいで連続させる
// 。 幅0は表示上の1px線、負・非有限の幅は不正とする。 破線が過大に展開され
// る場合はlength_errorを投げ、一括表示側は前のフレームを保持する。
[[nodiscard]] DrawingGeometry stroke_path(std::span<const Point> points, bool closed,
                                          float width, StrokeStyle style);
[[nodiscard]] DrawingGeometry polygon_geometry(std::span<const Point> points,
                                               FillRule rule = FillRule::Winding);
[[nodiscard]] std::vector<Point> rectangle_path(Rect rect);
[[nodiscard]] std::vector<Point> ellipse_path(Rect rect);

} // namespace cwxeditor::lib
