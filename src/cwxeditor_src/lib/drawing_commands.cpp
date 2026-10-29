#include "drawing_commands.hpp"
#include "image_mask.hpp"
#include <stdexcept>
#include <type_traits>

namespace cwxeditor::lib {
class DrawingCommandRecorder final : public DrawingContext {
public:
    explicit DrawingCommandRecorder(const DrawingCaptureServices& services):services_(services) {}
    DrawingCommandList finish() { return std::move(list_); }
    void save() override { add(DrawingSave{}); }
    void restore() override { add(DrawingRestore{}); }
    void clip(Rect r) override { add(DrawingClip{r}); }
    void set_antialias(bool value) override { add(DrawingAntialias{value}); }
    void clear(Color c) override { add(DrawingClear{c}); }
    void draw_line(Point a,Point b,Color c,float w,StrokeStyle s) override { add(DrawingLine{a,b,c,w,s}); }
    void draw_rect(Rect r,Color c,float w,StrokeStyle s) override { add(DrawingRect{r,c,w,s,false}); }
    void fill_rect(Rect r,Color c) override { add(DrawingRect{r,c,0,StrokeStyle::Solid,true}); }
    void draw_polygon(std::span<const Point> p,Color c,float w,StrokeStyle s) override {
        add(DrawingPolygon{{p.begin(),p.end()},c,w,s,FillRule::Winding,false});
    }
    void fill_polygon(std::span<const Point> p,Color c,FillRule rule) override {
        add(DrawingPolygon{{p.begin(),p.end()},c,0,StrokeStyle::Solid,rule,true});
    }
    void draw_ellipse(Rect r,Color c,float w,StrokeStyle s) override { add(DrawingEllipse{r,c,w,s,false}); }
    void fill_ellipse(Rect r,Color c) override { add(DrawingEllipse{r,c,0,StrokeStyle::Solid,true}); }
    void fill_gradient(Rect r,Color a,Color b,GradientAxis axis,CompositeMode mode) override {
        add(DrawingGradient{r,a,b,axis,mode});
    }
    void draw_text(std::string_view t,Point p,const FontSpec& f,Color c) override {
        add(DrawingText{std::string(t),p,f,c});
    }
    void draw_text_rotated(std::string_view t,Point p,const FontSpec& f,Color c,
                           float degrees) override {
        add(DrawingRotatedText{std::string(t),p,f,c,degrees});
    }
    void draw_image(ResourceId id,Rect s,Rect d,Interpolation i) override {
        if (!services_.retain_image) throw std::logic_error("Canvas image capture requires a resource provider");
        // 後で生成されても、このフレームで存在しなかった画像は存在しないままにする。
        if (auto image=services_.retain_image(id)) add(DrawingImage{std::move(image),s,d,i});
    }
    void draw_image_with_alpha(ResourceId id,Rect s,Rect d,Interpolation i,
                               std::uint8_t alpha) override {
        if (alpha == 255U) { draw_image(id,s,d,i); return; }
        if (!services_.retain_image) throw std::logic_error("Canvas image capture requires a resource provider");
        if (const auto source=services_.retain_image(id)) {
            auto image=std::make_shared<ImageData>(*source);
            apply_image_opacity(*image,alpha);
            add(DrawingImage{std::move(image),s,d,i});
        }
    }
    Size measure_text(std::string_view t,const FontSpec& f,float wrap) override {
        if (!services_.measure_text) throw std::logic_error("Canvas text measurement requires glyph metrics");
        return services_.measure_text(t,f,wrap);
    }
private:
    template<class T> void add(T value) { list_.commands_.emplace_back(std::move(value)); }
    const DrawingCaptureServices& services_;
    DrawingCommandList list_;
};
DrawingCommandList record_drawing(const std::function<void(DrawingContext&,Rect)>& paint,
    Rect bounds,const DrawingCaptureServices& services) {
    DrawingCommandRecorder recorder(services);
    if (paint) paint(recorder,bounds);
    return recorder.finish();
}
void DrawingCommandList::replay(DrawingContext& context,const DrawingImageReplay& images) const {
    for (const auto& command:commands_) std::visit([&](const auto& op) {
        using T=std::decay_t<decltype(op)>;
        if constexpr(std::is_same_v<T,DrawingSave>) context.save();
        else if constexpr(std::is_same_v<T,DrawingRestore>) context.restore();
        else if constexpr(std::is_same_v<T,DrawingClip>) context.clip(op.rect);
        else if constexpr(std::is_same_v<T,DrawingAntialias>) context.set_antialias(op.enabled);
        else if constexpr(std::is_same_v<T,DrawingClear>) context.clear(op.color);
        else if constexpr(std::is_same_v<T,DrawingLine>) context.draw_line(op.from,op.to,op.color,op.width,op.style);
        else if constexpr(std::is_same_v<T,DrawingRect>) {
            if(op.filled) context.fill_rect(op.rect,op.color);
            else context.draw_rect(op.rect,op.color,op.width,op.style);
        } else if constexpr(std::is_same_v<T,DrawingPolygon>) {
            if(op.filled) context.fill_polygon(op.points,op.color,op.rule);
            else context.draw_polygon(op.points,op.color,op.width,op.style);
        } else if constexpr(std::is_same_v<T,DrawingEllipse>) {
            if(op.filled) context.fill_ellipse(op.rect,op.color);
            else context.draw_ellipse(op.rect,op.color,op.width,op.style);
        } else if constexpr(std::is_same_v<T,DrawingGradient>) context.fill_gradient(op.rect,op.first,op.second,op.axis,op.mode);
        else if constexpr(std::is_same_v<T,DrawingText>) context.draw_text(op.text,op.origin,op.font,op.color);
        else if constexpr(std::is_same_v<T,DrawingRotatedText>) context.draw_text_rotated(op.text,op.origin,op.font,op.color,op.degrees);
        else if constexpr(std::is_same_v<T,DrawingImage>) {
            if(!images) throw std::logic_error("Canvas replay requires a retained-image painter");
            images(*op.image,op.source,op.destination,op.interpolation);
        }
    },command);
}
void DrawingCommandList::replay_owned(DrawingContext& context,const DrawingOwnedImageReplay& images) const {
    for (const auto& command:commands_) std::visit([&](const auto& op) {
        using T=std::decay_t<decltype(op)>;
        if constexpr(std::is_same_v<T,DrawingSave>) context.save();
        else if constexpr(std::is_same_v<T,DrawingRestore>) context.restore();
        else if constexpr(std::is_same_v<T,DrawingClip>) context.clip(op.rect);
        else if constexpr(std::is_same_v<T,DrawingAntialias>) context.set_antialias(op.enabled);
        else if constexpr(std::is_same_v<T,DrawingClear>) context.clear(op.color);
        else if constexpr(std::is_same_v<T,DrawingLine>) context.draw_line(op.from,op.to,op.color,op.width,op.style);
        else if constexpr(std::is_same_v<T,DrawingRect>) {
            if(op.filled) context.fill_rect(op.rect,op.color);
            else context.draw_rect(op.rect,op.color,op.width,op.style);
        } else if constexpr(std::is_same_v<T,DrawingPolygon>) {
            if(op.filled) context.fill_polygon(op.points,op.color,op.rule);
            else context.draw_polygon(op.points,op.color,op.width,op.style);
        } else if constexpr(std::is_same_v<T,DrawingEllipse>) {
            if(op.filled) context.fill_ellipse(op.rect,op.color);
            else context.draw_ellipse(op.rect,op.color,op.width,op.style);
        } else if constexpr(std::is_same_v<T,DrawingGradient>) context.fill_gradient(op.rect,op.first,op.second,op.axis,op.mode);
        else if constexpr(std::is_same_v<T,DrawingText>) context.draw_text(op.text,op.origin,op.font,op.color);
        else if constexpr(std::is_same_v<T,DrawingRotatedText>) context.draw_text_rotated(op.text,op.origin,op.font,op.color,op.degrees);
        else if constexpr(std::is_same_v<T,DrawingImage>) {
            if(!images) throw std::logic_error("Canvas replay requires a retained-image painter");
            images(op.image,op.source,op.destination,op.interpolation);
        }
    },command);
}
} // namespace cwxeditor::lib
