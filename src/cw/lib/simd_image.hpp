#pragma once

#include <cstddef>
#include <cstdint>

namespace cw::lib::simd_image {

// pygame 形式の 8 ビット RGBA サ
// ーフェス操作に対応する。キャッシュを持たず、未
// 対応 CPU は厳密に同じスカラー演算を使う。
const char* backend_name() noexcept;

// pygame の JPEG 保存に使う、黒背景上への SDL1 の RGB24 変
// 換。入力 RGBA と出力 RGB は重ならないバッファーを使う。RGB サーフェ
// スは格納されたアルファを無視し、画素数ゼロなら null ポインターを許可する。
void rgba_to_rgb_black(const std::uint8_t* source, std::uint8_t* destination,
                       std::size_t pixel_count, bool has_pixel_alpha);

// pygame 1.9.6 の GENERIC smoothscale の
// 演算。横、縦の順に処理し、縮小は 16.16 の面積平均、拡大は端点イン
// デックスによる補間を使う。RGBA の各成分をアルファも含め独立して計
// 算する。中間行・画像を確保する場合があり、寸法は正でなければならない。
void smoothscale_rgba(const std::uint8_t* source, std::size_t source_width,
                      std::size_t source_height, std::uint8_t* destination,
                      std::size_t width, std::size_t height);

void fill_rgba(std::uint8_t* pixels, std::size_t pixel_count,
               std::uint8_t red, std::uint8_t green,
               std::uint8_t blue, std::uint8_t alpha) noexcept;
void force_alpha(std::uint8_t* pixels, std::size_t pixel_count,
                 std::uint8_t alpha) noexcept;
void multiply_alpha(std::uint8_t* pixels, std::size_t pixel_count,
                    std::uint8_t multiplier) noexcept;
// pygame 1.9.6 の (255,255,255,alpha) による BLEND_RGBA_MULT。SDL_SRCALPHA
// が有効でもアルファマスクのない RGB32 は affect_alpha=false を使う。
void multiply_rgba_alpha(std::uint8_t* pixels, std::size_t pixel_count,
                         std::uint8_t multiplier, bool affect_alpha = true) noexcept;
void subtract_alpha(std::uint8_t* pixels, std::size_t pixel_count,
                    std::uint8_t amount) noexcept;
void invert_rgb(std::uint8_t* pixels, std::size_t pixel_count) noexcept;
void xor_rgb(std::uint8_t* pixels, std::size_t pixel_count,
             std::uint8_t first, std::uint8_t second, std::uint8_t third) noexcept;
// Editor/D 用の、正規化した非乗算アルファの source-over 合成。最近値に丸める。RGBA/BGRA
// の違いは swap_source_red_blue で選び、バッファーは重ねない。
void composite_straight_alpha(const std::uint8_t* source, std::uint8_t* destination,
                             std::size_t pixel_count, bool swap_source_red_blue) noexcept;
void swap_red_blue(const std::uint8_t* source, std::uint8_t* destination,
                   std::size_t pixel_count) noexcept;
void colorkey_alpha_zero(std::uint8_t* pixels, std::size_t pixel_count,
                         std::uint8_t red, std::uint8_t green,
                         std::uint8_t blue, bool clear_rgb = false) noexcept;
// clear_rgb は pygame の RGB とカラーキーからの convert_alpha()
// に合わせ、キー色の画素を透明な黒にする。通常のカラーキー適用では RGB を保つ。

// pygame 1.9.6 の通常の SRCALPHA 転送。dst.a==0 なら非乗算
// RGBA を複製し、それ以外は rgb=(((src-dst)*a+src)>>8)+dst
// 、alpha=a+dst.a-a*dst.a/255。ストライドはバイト単位で width*4
// を超えてよい。RGB→RGBA は画素アルファを全体の不透明度に置き換えられる。RGBA
// →RGB は SDL の画素ごとの RGB 演算を使い、出力先のアルファ欄を保つ。
void alpha_blend_rgba(const std::uint8_t* source, std::size_t source_stride,
                      std::uint8_t* destination, std::size_t destination_stride,
                      std::size_t width, std::size_t height,
                      std::uint8_t surface_alpha = 255,
                      bool replace_pixel_alpha = false,
                      bool rgb_destination = false) noexcept;

// pygame 1.9.6 / SDL の、SRCALPHA なしの面全体のアルフ
// ァ。元の画素バイトは変えず、SDL と同様に出力先アルファを保つ。論理的な既定の
// RGB565 入力では、SDL1 のビットシフトと複製用の変換表を再現する。
void surface_alpha_blend_rgba(const std::uint8_t* source, std::size_t source_stride,
                              std::uint8_t* destination, std::size_t destination_stride,
                              std::size_t width, std::size_t height,
                              std::uint8_t alpha, bool generic_rgb_rounding = false,
                              bool rgb565_source = false, bool rgb565_key = false) noexcept;

// pygame の BLEND_MIN/BLEND_ADD は、画素アルファがゼロでも RGB
// を演算する。アルファ、全体の不透明度、カラーキーで処理を制限しない。バッファーは重ねない。
void blend_rgb_rgba(const std::uint8_t* source, std::size_t source_stride,
                     std::uint8_t* destination, std::size_t destination_stride,
                     std::size_t width, std::size_t height, bool add) noexcept;

} // namespace cw::lib::simd_image
