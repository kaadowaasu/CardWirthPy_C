#pragma once
#include "graphics.hpp"
#include <functional>
#include <variant>

namespace cwxeditor::lib {
// OSに依存しない所有権付きの描画命令。 再生時までコールバック・
// 借用文字列・ポリゴンの借用範囲・可変の画像プール項目を残さない。
struct DrawingCaptureServices {
    std::function<Size(std::string_view,const FontSpec&,float)> measure_text;
    // 不変の世代を返す。資源の更新・破棄はプール項目の置き換
    // え・解放で行い、 フレームが保持する画素を変更しない。
    std::function<std::shared_ptr<const ImageData>(ResourceId)> retain_image;
};
struct DrawingSave {};
struct DrawingRestore {};
struct DrawingClip { Rect rect; };
struct DrawingAntialias { bool enabled{}; };
struct DrawingClear { Color color; };
struct DrawingLine { Point from,to; Color color; float width; StrokeStyle style; };
struct DrawingRect { Rect rect; Color color; float width; StrokeStyle style; bool filled; };
struct DrawingPolygon { std::vector<Point> points; Color color; float width; StrokeStyle style; FillRule rule; bool filled; };
struct DrawingEllipse { Rect rect; Color color; float width; StrokeStyle style; bool filled; };
struct DrawingGradient { Rect rect; Color first,second; GradientAxis axis; CompositeMode mode; };
struct DrawingText { std::string text; Point origin; FontSpec font; Color color; };
struct DrawingRotatedText { std::string text; Point origin; FontSpec font; Color color; float degrees{}; };
struct DrawingImage { std::shared_ptr<const ImageData> image; Rect source,destination; Interpolation interpolation; };
using DrawingCommand=std::variant<DrawingSave,DrawingRestore,DrawingClip,DrawingAntialias,DrawingClear,
    DrawingLine,DrawingRect,DrawingPolygon,DrawingEllipse,DrawingGradient,DrawingText,DrawingRotatedText,DrawingImage>;
using DrawingImageReplay=std::function<void(const ImageData&,Rect,Rect,Interpolation)>;
using DrawingOwnedImageReplay=std::function<void(const std::shared_ptr<const ImageData>&,Rect,Rect,Interpolation)>;
class DrawingCommandList {
public:
    // コンテキストは描画だけを行い、画像の再生には保持済みの画素を使う。
    void replay(DrawingContext& context,const DrawingImageReplay& images) const;
    void replay_owned(DrawingContext& context,const DrawingOwnedImageReplay& images) const;
    [[nodiscard]] const std::vector<DrawingCommand>& commands() const { return commands_; }
private:
    friend class DrawingCommandRecorder;
    std::vector<DrawingCommand> commands_;
};
[[nodiscard]] DrawingCommandList record_drawing(
    const std::function<void(DrawingContext&,Rect)>& paint,Rect bounds,
    const DrawingCaptureServices& services);
} // namespace cwxeditor::lib
