#pragma once
#include "cwxeditor_src/core/cwx/types.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/lib/graphics.hpp"
#include "cwxeditor_src/lib/image_text.hpp"
#include "cwxeditor_src/lib/window_host.hpp"
#include "cwxeditor_src/lib/text_path.hpp"
#include "cwxeditor_src/lib/image_composition.hpp"
#include "cwxeditor_src/core/cwx/graphics.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <string>
#include <tuple>
namespace cwxeditor::cwx::editor::gui::dwt::cellrender {
namespace lib=::cwxeditor::lib;
using Current=std::function<bool()>;
inline bool live(const Current& current){return !current || current();}
inline bool draw_selection_rect(lib::DrawingContext& drawing,lib::Rect bounds,bool xor_line,
    float width,const Current& current={}){
 if(!live(current))return false;
 bool drawn=false;
 if(xor_line){drawn=drawing.draw_xor_rect({bounds.x,bounds.y,std::max(0.F,bounds.width-1),
     std::max(0.F,bounds.height-1)},{255,255,255,255},width);if(!live(current))return false;}
 // DのXOR描画には、読み取り可能な描画コンテキストが必要。
 // 記録用・未対応の実装では、 XORしたように白い実線を出す
 // のではなく、共通の代替処理で従来のフォーカス輪郭を描く。
 if(!drawn){drawing.draw_rect(bounds,{0,96,224,255},width,lib::StrokeStyle::Dash);if(!live(current))return false;}
 return true;
}
struct Text {
 std::string value;
 lib::FontSpec font;
 lib::Color foreground,outline;
 types::BorderingType border{types::BorderingType::None};
 float border_width{1};
 bool vertical{};
 std::shared_ptr<lib::GraphicsBackend> graphics;
 bool title_antialias{};
 // 公開の画像データAPIは、転送前のこの同じ共有画像を保持できる。
 std::function<void(lib::ImageData)> raster_output;
};
struct TextRasterCache {
 Text text;
 lib::Size size{};
 lib::Point offset{};
 lib::FontMetrics metrics{};
 std::shared_ptr<lib::OwnedImageResource> resource;
 unsigned width{},height{};
 std::uint64_t revision{};
 bool preserve_inline_alpha{};
};
inline auto font_key(const lib::FontSpec& f){return std::tie(f.family,f.points,f.weight,f.italic,
 f.underline,f.strikeout,f.antialias,f.content_font,f.decorations_in_policy,f.size_mode);}
inline auto color_key(const lib::Color& c){return std::tie(c.red,c.green,c.blue,c.alpha);}
inline bool matches(const TextRasterCache& c,const Text& t,lib::Rect clip,lib::Point origin,lib::FontMetrics m,bool preserve_inline_alpha=false){
 return c.preserve_inline_alpha==preserve_inline_alpha && c.resource && c.resource->id && c.text.graphics==t.graphics && c.text.value==t.value &&
  font_key(c.text.font)==font_key(t.font) && color_key(c.text.foreground)==color_key(t.foreground) &&
  color_key(c.text.outline)==color_key(t.outline) && c.text.border==t.border && c.text.border_width==t.border_width &&
  c.text.vertical==t.vertical && c.text.title_antialias==t.title_antialias &&
  c.size.width==clip.width && c.size.height==clip.height && c.offset.x==origin.x-clip.x && c.offset.y==origin.y-clip.y &&
  c.metrics.height==m.height && c.metrics.ascent==m.ascent && c.metrics.descent==m.descent;
}
inline void draw_cached_raster(lib::DrawingContext& drawing,lib::Rect clip,
 std::shared_ptr<lib::OwnedImageResource> resource,unsigned width,unsigned height,const Current& current){
 if(!resource || !resource->id || !live(current))return;
 drawing.save();struct Restore{lib::DrawingContext& draw;~Restore(){draw.restore();}}restore{drawing};if(!live(current))return;
 drawing.clip(clip);if(!live(current))return;
 drawing.draw_image(resource->id,{0,0,float(width),float(height)},clip,lib::Interpolation::Nearest);
}

inline void draw_raster(lib::DrawingContext& drawing,lib::Rect clip,lib::ImageFrame canvas,
    const Text& text,const Current& current,const std::shared_ptr<TextRasterCache>& cache={}){
 const auto width=canvas.pixel_width,rows=canvas.pixel_height;
 if(!width||!rows||!live(current))return;
 if(text.vertical){
  lib::Bytes bytes(canvas.pixels.size()),alpha(std::size_t(width)*rows);
  for(std::size_t i=0;i<bytes.size();++i)bytes[i]=std::to_integer<lib::Byte>(canvas.pixels[i]);
  for(std::size_t i=0;i<alpha.size();++i)alpha[i]=bytes[i*4+3];
  std::size_t w=width,h=rows,stride=std::size_t(width)*4;
  // DのTurn.LEFT（値1）に対応する共通変換。(x,y)を(height-1-y,x)へ移し、画面上では時計回りにする。
  graphics::turn(bytes,alpha,w,h,stride,static_cast<graphics::Turn>(1),32);
  canvas.pixel_width=static_cast<unsigned>(w);canvas.pixel_height=static_cast<unsigned>(h);canvas.logical_size={float(w),float(h)};
  for(std::size_t i=0;i<bytes.size();++i)canvas.pixels[i]=std::byte(bytes[i]);
  for(std::size_t i=0;i<alpha.size();++i)canvas.pixels[i*4+3]=std::byte(alpha[i]);
 }
 lib::ImageData image;image.frames={std::move(canvas)};
 if(text.raster_output){text.raster_output(std::move(image));return;}
 auto resource=std::make_shared<lib::OwnedImageResource>(text.graphics);
 resource->id=text.graphics->create_image(image);if(!live(current)||!resource->id)return;
 if(cache){cache->resource=resource;cache->width=image.frames[0].pixel_width;cache->height=image.frames[0].pixel_height;}
 draw_cached_raster(drawing,clip,std::move(resource),image.frames[0].pixel_width,image.frames[0].pixel_height,current);
}
inline void draw_aa_text(lib::DrawingContext& drawing,lib::Rect clip,lib::Point origin,
    const Text& text,const std::vector<std::string>& lines,float height,int stroke,
    int underline,int strike,const Current& current,const std::shared_ptr<TextRasterCache>& cache={}){
 const auto dimension=[](float value){
  if(!std::isfinite(value)||value<=0||value>std::numeric_limits<int>::max()-2)return 0U;
  return static_cast<unsigned>(std::ceil(value));
 };
 // DのshrinkDrawTextはRGBからAA文字を着色し、指
 // 定された前景色のアルファを被覆率へ掛けない。 指定色はキャッシュ
 // キーに保ち、独立した輪郭のアルファとALLの既存規則も維持する。
 auto foreground=text.foreground;
 if(!lib::editor_window_host_options().all_mode)foreground.alpha=255;
 const auto width=dimension(text.vertical?clip.height:clip.width),rows=dimension(text.vertical?clip.width:clip.height);
 if(!width||!rows)return;
 if(std::uint64_t(width)*rows>std::numeric_limits<std::size_t>::max()/4)throw std::length_error("TextCell raster allocation");
 lib::ImageFrame canvas;canvas.pixel_width=width;canvas.pixel_height=rows;canvas.logical_size={float(width),float(rows)};
 canvas.pixel_format=lib::PixelFormat::RGBA32;canvas.pixels.resize(std::size_t(width)*rows*4);
 const lib::Rect local_clip{0,0,float(width),float(rows)};
 lib::Point point{origin.x-clip.x,origin.y-clip.y};
 for(const auto& line:lines){
  const auto extent=text.graphics->measure_text(line,text.font,0);if(!live(current))return;
  if(!line.empty()&&extent.width>0&&extent.height>0){
   lib::ImageTextStyle style{text.font,foreground,
       text.border==types::BorderingType::Outline?text.outline:lib::Color{0,0,0,0},0,true};
   style.glyph_scale=2;style.target_size=extent;style.current=current;
   const auto raster=lib::rasterize_image_text(text.graphics,line,std::move(style));if(!live(current))return;
   if(raster.pixel_width)(void)lib::composite_image_pixels(canvas,raster,{point.x-1,point.y-1},local_clip);
  }
  const auto decoration=[&](int position){
   const auto pixels=dimension(extent.width);if(!pixels)return;
   if(std::uint64_t(pixels)*stroke>std::numeric_limits<std::size_t>::max()/4)throw std::length_error("TextCell decoration allocation");
   lib::ImageFrame bar;bar.pixel_width=pixels;bar.pixel_height=stroke;bar.pixel_format=lib::PixelFormat::RGBA32;bar.pixels.resize(std::size_t(pixels)*stroke*4);
   for(std::size_t i=0;i<bar.pixels.size();i+=4){bar.pixels[i]=std::byte(foreground.red);bar.pixels[i+1]=std::byte(foreground.green);bar.pixels[i+2]=std::byte(foreground.blue);bar.pixels[i+3]=std::byte(foreground.alpha);}
   (void)lib::composite_image_pixels(canvas,bar,{0,point.y+position},local_clip);
  };
  if(text.font.underline)decoration(underline);if(text.font.strikeout)decoration(strike);
  point.y+=height;
 }
 draw_raster(drawing,clip,std::move(canvas),text,current,cache);
}
// 通常の場面と公開画像APIで同じ描画規則を使う。 文字の形と回
// 転はOS側の基本操作だが、行分割・間隔・縁取りは共通にする。
inline void draw_text(lib::DrawingContext& drawing,lib::Rect clip,lib::Point origin,Text text,Current current={},
 const std::shared_ptr<TextRasterCache>& cache={}){
 if(text.value.empty() || !live(current))return;
 const bool preserve_inline_alpha=!lib::editor_window_host_options().all_mode && text.border==types::BorderingType::Inline;
 if(preserve_inline_alpha){
  // DのredToAlphaは文字の被覆率とRGBを使い、Inlineの文字・枠
  // では指定色のアルファを捨てる。 ALLのアルファを考慮する拡張は変えない。
  text.foreground.alpha=255;
  text.outline.alpha=255;
 }
 const auto lines=utils::splitLines(text.value);
 const auto metrics=drawing.font_metrics(text.font);
 if(!live(current))return;
 if(!std::isfinite(metrics.height)||!std::isfinite(metrics.ascent)||!std::isfinite(metrics.descent)||
    !std::isfinite(text.font.points))return;
 const float height=std::isfinite(metrics.height)?std::max(1.0F,metrics.height):1.0F;
 // バックエンドからの任意の寸法は、Dのintの巻き戻りでなく範囲
 // 付きで変換する。 通常の指定ピクセル値にはDの整数式を使う。
 const auto integer=[](double value){return static_cast<int>(std::clamp(value,
    double(std::numeric_limits<int>::min()),double(std::numeric_limits<int>::max())));};
 // DのPileImageは初期値0の_fontPixelSize
 // を変更しないため、装飾の太さはフォントによらず1となる。 単
 // 独起動の公開API・場面描画では同じ太さにし、 ALLでは
 // 指定フォントサイズに合わせて太さを変える既存の拡張を保つ。
 const int stroke=lib::editor_window_host_options().all_mode
    ? std::max(1,integer(double(text.font.points)/16)) : 1;
 const int underline=integer(double(height)-metrics.descent+stroke/2);
 const int strike=integer(double(height)-integer(metrics.ascent)/2+double(stroke/2));
 // DのlineMetricsが装飾矩形を与えるため、 フォン
 // トエンジン固有の位置にネイティブの下線を重ねて描かない。
 text.font.decorations_in_policy=true;
 if(cache && text.graphics && (text.border==types::BorderingType::Inline || text.font.antialias)){
  if(matches(*cache,text,clip,origin,metrics,preserve_inline_alpha)){
   draw_cached_raster(drawing,clip,cache->resource,cache->width,cache->height,current);return;
  }
  // Dと同じく、入力が変わるまで生成済みのInline TextCell画像を保持す
  // る。 各呼び出し元が同じ共通規則のキャッシュを持ち、ホスト専用の経路を作らない。
  const auto generation=++cache->revision;
  cache->resource.reset();
  cache->preserve_inline_alpha=preserve_inline_alpha;
  cache->text=text;cache->size={clip.width,clip.height};cache->offset={origin.x-clip.x,origin.y-clip.y};cache->metrics=metrics;
  current=[cache,generation,previous=std::move(current)]{return cache->revision==generation && live(previous);};
 }
 if(text.border==types::BorderingType::Inline && text.graphics){
  lib::InlineTextStyle style{text.font,text.foreground,text.outline,text.border_width,height,
      stroke,underline,strike,text.font.antialias,current};
  style.font.antialias=text.title_antialias;
  style.preserve_foreground_alpha=preserve_inline_alpha;
  const auto raster=lib::rasterize_inline_text(text.graphics,lines,
      {text.vertical?clip.height:clip.width,text.vertical?clip.width:clip.height},std::move(style));
  if(!live(current))return;
  if(raster){draw_raster(drawing,clip,std::move(*raster),text,current,cache);return;}
 }
 if(text.font.antialias && text.graphics && text.border!=types::BorderingType::Inline){
  draw_aa_text(drawing,clip,origin,text,lines,height,stroke,underline,strike,current,cache);return;
 }
 drawing.save();
 struct Restore{lib::DrawingContext& drawing;~Restore(){drawing.restore();}}restore{drawing};
 if(!live(current))return;
 drawing.clip(clip);if(!live(current))return;
 const auto paint_lines=[&](float x,float y,lib::Color color){
  auto point=origin;
  if(text.vertical)point.x=clip.x+clip.width-height;
  for(const auto& line:lines){
   if(!live(current))return;
   // DのTurn.LEFTは(x,y)を(height-1-y,x)へ変換し、画面上では時計回りに回転する。
   if(text.vertical)drawing.draw_text_rotated(line,{point.x+x,point.y+y},text.font,color,90);
   else drawing.draw_text(line,{point.x+x,point.y+y},text.font,color);
   if(!live(current))return;
   if(text.font.underline || text.font.strikeout){
    const auto extent=drawing.measure_text(line,text.font,0);if(!live(current))return;
    const auto decoration=[&](int position){
     // DのdrawTextImplの矩形は、輪郭を描く場合もx=0から始める。
     const lib::Rect rect=text.vertical
        ? lib::Rect{point.x+height-position-stroke-y,origin.y,float(stroke),extent.width}
        : lib::Rect{origin.x,point.y+y+position,extent.width,float(stroke)};
     drawing.fill_rect(rect,color);
    };
    if(text.font.underline){decoration(underline);if(!live(current))return;}
    if(text.font.strikeout){decoration(strike);if(!live(current))return;}
   }
   if(text.vertical)point.x-=height;else point.y+=height;
  }
 };
 if(text.border==types::BorderingType::Outline){
   // DのAAなしのOutlineは、フォント倍率やInline専用のborderingWidth
   // によらず、 出力の1ピクセル分だけ四つの斜め方向へずらして描く。
   for(const auto offset:{lib::Point{-1,-1},lib::Point{-1,1},lib::Point{1,-1},lib::Point{1,1}}){
    paint_lines(offset.x,offset.y,text.outline);if(!live(current))return;
   }
  }else if(text.border==types::BorderingType::Inline){
   // ネイティブの文字輪郭がない記録用などの提供元に限り、
   // 既存のラスタ近似を代替処理として残す。 動作中のSDL
   // と通常の全場面は、共通のD形式の輪郭処理を使う。
   const int width=std::max(1,static_cast<int>(std::lround(text.border_width)));
   for(int y=-width;y<=width;++y)for(int x=-width;x<=width;++x)if(x||y){
    paint_lines(static_cast<float>(x),static_cast<float>(y),text.outline);if(!live(current))return;
   }
  }
 // Dと同じく、全輪郭を描いてから全前景行を描く。 後の縁
 // 取りで隣の文字の描画済み前景を上書きしないようにする。
 paint_lines(0,0,text.foreground);
}
struct PC {
 lib::Rect clip,ellipse;
 lib::FontSpec font;
 lib::Color foreground,background;
 std::string number;
};
inline void draw_pc(lib::DrawingContext& drawing,PC cell,const Current& current={}){
 if(!live(current))return;
 drawing.save();
 struct Restore{lib::DrawingContext& drawing;~Restore(){drawing.restore();}}restore{drawing};
 if(!live(current))return;
 drawing.clip(cell.clip);if(!live(current))return;
 // DのDrawerは合成済みバッファーを複製し、この楕円の背後を別の
 // 矩形色では塗らない。 色のアルファは共通の描画操作で引き続き扱う。
 drawing.fill_ellipse(cell.ellipse,cell.background);if(!live(current))return;
 auto inside=cell.ellipse;inside.width=std::max(0.0F,inside.width-1);inside.height=std::max(0.0F,inside.height-1);
 drawing.draw_ellipse(inside,cell.foreground,1,lib::StrokeStyle::Solid);if(!live(current))return;
 const auto measured=drawing.measure_text(cell.number,cell.font,0);if(!live(current))return;
 // Dと同じく、width-1/height-1の内側で符号付き整数除算により中央へ置
 // く。 大きな文字列は楕円より手前から始まる場合があり、実際のセル範囲で切り取る。
 const lib::Point point{inside.x+(static_cast<int>(inside.width)-static_cast<int>(measured.width))/2,
                        inside.y+(static_cast<int>(inside.height)-static_cast<int>(measured.height))/2};
 drawing.draw_text(cell.number,point,cell.font,cell.foreground);
}
} // namespace cwxeditor::cwx::editor::gui::dwt::cellrender
