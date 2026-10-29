#pragma once

// cw/imageretouch.pyの画像加工・フォントAPIの、移植可能な宣言。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"

namespace cw::imageretouch {

// ネイティブ・UIの状態は小さなアダプターで公開する。 共通のcompat::Surface
// をデータモデルとし、SDL/wx/Win32側が 自分で管理
// するメタデータと座標・フォントの規則を登録できるようにする。
struct SurfaceMetadata {
    bool has_colorkey{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> colorkey{};
    bool depth1{};
    ::cw::lib::Real logical_scale{1.0};
};

using GlyphRenderAdapter = std::function<std::shared_ptr<::cw::lib::Surface>(
    const ::cw::lib::String&, ::cw::lib::Integer, bool, bool, bool,
    const ::cw::lib::String&, bool,
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>)>;
using LogicalScaleAdapter = std::function<::cw::lib::Integer(::cw::lib::Integer)>;
using FontFaceAdapter = std::function<::cw::lib::String(const ::cw::lib::String&)>;

void set_glyph_render_adapter(GlyphRenderAdapter arg_adapter);
void set_logical_scale_adapter(LogicalScaleAdapter arg_adapter);
void set_font_face_adapter(FontFaceAdapter arg_adapter);
void set_surface_metadata(const std::shared_ptr<::cw::lib::Surface>& arg_surface,
                          SurfaceMetadata arg_metadata);
SurfaceMetadata get_surface_metadata(const std::shared_ptr<const ::cw::lib::Surface>& arg_surface);
void clear_surface_metadata(const std::shared_ptr<::cw::lib::Surface>& arg_surface);

class Font : public ::cw::lib::Object {
    enum class RasterSource { Unresolved, TrueType, Native, Placeholder };
    RasterSource m_raster_source{RasterSource::Unresolved};
    ::cw::lib::String m_raster_file;
    void select_raster_source();
public:
    Font() = default;
    virtual ~Font() = default;
    inline static std::map<std::tuple<::cw::lib::String, bool, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>>, std::shared_ptr<::cw::lib::Surface>> s__cache{};
    inline static std::optional<std::shared_ptr<::cw::lib::Font>> s_font{};
    inline static std::optional<std::shared_ptr<::cw::lib::Font>> s_font2x{};
    inline static std::optional<std::shared_ptr<::cw::lib::Font>> s_font_notitalic{};
    inline static std::optional<::cw::lib::Value> s_fontinfo{};
    inline static std::optional<::cw::lib::Value> s_fontinfo2x{};
    ::cw::lib::ValueMap m__cache{};
    ::cw::lib::Value m_bold{};
    ::cw::lib::Value m_face{};
    ::cw::lib::Value m_font{};
    ::cw::lib::Value m_font2x{};
    ::cw::lib::Value m_font_notitalic{};
    ::cw::lib::Value m_fontinfo{};
    ::cw::lib::Value m_fontinfo2x{};
    ::cw::lib::Value m_italic{};
    // Android/Linuxの可読性補正を適用する前の要求ピクセル数。
    // 複製やスーパーサンプリングでもこの値を保ち、補正を重複適用しない。
    ::cw::lib::Value m_pixels{};
    bool m_underline{};
    void initialize(::cw::lib::String arg_face, ::cw::lib::Integer arg_pixels, bool arg_bold, bool arg_italic);
    // wxダイアログの寸法にはホストの文字計測を使い、pygameのゲーム
    // 用フォント選択と分離する。 Fontオブジェクトの構造は変えない。
    void initialize_wx(::cw::lib::String arg_face, ::cw::lib::Integer arg_pixels, bool arg_bold, bool arg_italic);
    bool _is_cachable(::cw::lib::String arg_s);
    void dispose();
    void py_del();
    bool get_bold();
    void set_bold(bool arg_v);
    bool get_italic();
    void set_italic(bool arg_v);
    bool get_underline();
    void set_underline(bool arg_v);
    [[nodiscard]] ::cw::lib::Integer get_raster_pixels() const;
    ::cw::lib::Integer get_height();
    ::cw::lib::Integer get_linesize();
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> size(::cw::lib::String arg_text);
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> size_withoutoverhang(::cw::lib::String arg_text);
    // 最初と最後の文字の左右の余白を含む、wx.DCのダイアログ文字列サイズ。
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> wx_text_extent(::cw::lib::String arg_text);
    std::shared_ptr<::cw::lib::Surface> render(::cw::lib::String arg_text, bool arg_antialias, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_colour);
    std::shared_ptr<::cw::lib::Surface> render_sbold(::cw::lib::String arg_text, bool arg_antialias, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_colour);
    std::shared_ptr<::cw::lib::Surface> _render_impl(::cw::lib::String arg_text, bool arg_antialias, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_colour, bool arg_sbold);
};

extern ::cw::lib::Value g__RetouchArg1;
extern ::cw::lib::Value g__RetouchArg2;
extern ::cw::lib::Value g__RetouchArg3;

std::shared_ptr<::cw::lib::Surface> _retouch(::cw::lib::Callback arg_func, std::shared_ptr<::cw::lib::Surface> arg_image);
std::shared_ptr<::cw::lib::Surface> _retouch_2(::cw::lib::Callback arg_func, std::shared_ptr<::cw::lib::Surface> arg_image, ::cw::lib::Value arg_py_arg1);
std::shared_ptr<::cw::lib::Surface> _retouch_3(::cw::lib::Callback arg_func, std::shared_ptr<::cw::lib::Surface> arg_image, ::cw::lib::Value arg_py_arg1, ::cw::lib::Value arg_py_arg2);
std::shared_ptr<::cw::lib::Surface> _retouch_4(::cw::lib::Callback arg_func, std::shared_ptr<::cw::lib::Surface> arg_image, ::cw::lib::Value arg_py_arg1, ::cw::lib::Value arg_py_arg2, ::cw::lib::Value arg_py_arg3);
std::shared_ptr<::cw::lib::Surface> _retouch_5(::cw::lib::Callback arg_func, std::shared_ptr<::cw::lib::Surface> arg_image, const ::cw::lib::Arguments& arg_args);
std::shared_ptr<::cw::lib::Surface> to_negative(std::shared_ptr<::cw::lib::Surface> arg_image);
std::shared_ptr<::cw::lib::Surface> to_negative_for_card(std::shared_ptr<::cw::lib::Surface> arg_image, ::cw::lib::Integer arg_framewidth);
std::shared_ptr<::cw::lib::Bitmap> to_negative_for_wxcard(std::shared_ptr<::cw::lib::Bitmap> arg_wxbmp, ::cw::lib::Integer arg_framewidth);
std::shared_ptr<::cw::lib::Surface> add_lightness(std::shared_ptr<::cw::lib::Surface> arg_image, ::cw::lib::Integer arg_value);
std::shared_ptr<::cw::lib::Surface> add_mosaic(std::shared_ptr<::cw::lib::Surface> arg_image, ::cw::lib::Integer arg_value);
std::shared_ptr<::cw::lib::Surface> to_binaryformat(std::shared_ptr<::cw::lib::Surface> arg_image, ::cw::lib::Integer arg_value, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_basecolor);
std::shared_ptr<::cw::lib::Surface> add_noise(std::shared_ptr<::cw::lib::Surface> arg_image, ::cw::lib::Integer arg_value, bool arg_colornoise);
std::shared_ptr<::cw::lib::Surface> exchange_rgbcolor(std::shared_ptr<::cw::lib::Surface> arg_image, ::cw::lib::String arg_colormodel);
std::shared_ptr<::cw::lib::Surface> to_grayscale(std::shared_ptr<::cw::lib::Surface> arg_image);
std::shared_ptr<::cw::lib::Surface> to_sepiatone(std::shared_ptr<::cw::lib::Surface> arg_image, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_color);
std::shared_ptr<::cw::lib::Surface> spread_pixels(std::shared_ptr<::cw::lib::Surface> arg_image);
std::shared_ptr<::cw::lib::Surface> _filter(std::shared_ptr<::cw::lib::Surface> arg_image, std::tuple<std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>> arg_weight, ::cw::lib::Integer arg_offset, ::cw::lib::Integer arg_div);
std::shared_ptr<::cw::lib::Surface> filter_shape(std::shared_ptr<::cw::lib::Surface> arg_image);
std::shared_ptr<::cw::lib::Surface> filter_sharpness(std::shared_ptr<::cw::lib::Surface> arg_image);
std::shared_ptr<::cw::lib::Surface> filter_sunpower(std::shared_ptr<::cw::lib::Surface> arg_image);
std::shared_ptr<::cw::lib::Surface> filter_emboss(std::shared_ptr<::cw::lib::Surface> arg_image);
std::shared_ptr<::cw::lib::Surface> filter_coloremboss(std::shared_ptr<::cw::lib::Surface> arg_image);
std::shared_ptr<::cw::lib::Surface> filter_darkemboss(std::shared_ptr<::cw::lib::Surface> arg_image);
std::shared_ptr<::cw::lib::Surface> filter_electrical(std::shared_ptr<::cw::lib::Surface> arg_image);
std::shared_ptr<::cw::lib::Surface> add_transparentline(std::shared_ptr<::cw::lib::Surface> arg_image, bool arg_vline, bool arg_hline, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>> arg_rect, bool arg_setalpha);
std::shared_ptr<::cw::lib::Surface> add_transparentmesh(std::shared_ptr<::cw::lib::Surface> arg_image, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>> arg_rect, bool arg_setalpha);
void add_border(std::shared_ptr<::cw::lib::Surface> arg_img, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_bordercolor, ::cw::lib::Integer arg_borderwidth);
void blend_1_50(std::shared_ptr<::cw::lib::Surface> arg_dest, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos, std::shared_ptr<::cw::lib::Surface> arg_source, ::cw::lib::Integer arg_flag);
std::shared_ptr<::cw::lib::Bitmap> to_disabledimage(std::shared_ptr<::cw::lib::Bitmap> arg_wxbmp, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_maskpos);
void _to_disabledimage(::cw::lib::Bytes& arg_buf, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size);
std::shared_ptr<::cw::lib::Surface> to_disabledsurface(std::shared_ptr<::cw::lib::Surface> arg_image);
std::shared_ptr<::cw::lib::Bitmap> add_lightness_for_wxbmp(std::shared_ptr<::cw::lib::Bitmap> arg_wxbmp, ::cw::lib::Integer arg_lightness, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_maskpos);
void _add_lightness(::cw::lib::Bytes& arg_buf, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size, ::cw::lib::Integer arg_lightness);
std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> hex2color(::cw::lib::Integer arg_hexnum);
::cw::lib::Integer colorwrap(::cw::lib::Integer arg_num);
::cw::lib::Bytes decode_rle4data(::cw::lib::Bytes arg_data, ::cw::lib::Integer arg_h, ::cw::lib::Integer arg_bpl);
::cw::lib::Bytes decode_rle8data(::cw::lib::Bytes arg_data, ::cw::lib::Integer arg_h, ::cw::lib::Integer arg_bpl);
std::shared_ptr<::cw::lib::Surface> patch_alphadata(std::shared_ptr<::cw::lib::Surface> arg_image, ::cw::lib::String arg_ext, ::cw::lib::Bytes arg_data);
std::shared_ptr<::cw::lib::Image> mul_wxalpha(std::shared_ptr<::cw::lib::Image> arg_wximg, ::cw::lib::Integer arg_alpha);
std::shared_ptr<::cw::lib::Surface> mul_alpha(std::shared_ptr<::cw::lib::Surface> arg_image, ::cw::lib::Integer arg_alpha);
void blit_2bitbmp_to_card(std::shared_ptr<::cw::lib::Surface> arg_dest, std::shared_ptr<::cw::lib::Surface> arg_source, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
void blit_2bitbmp_to_message(std::shared_ptr<::cw::lib::Surface> arg_dest, std::shared_ptr<::cw::lib::Surface> arg_source, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_wincolour);
void wxblit_2bitbmp_to_card(std::shared_ptr<::cw::lib::Window> arg_dc, std::shared_ptr<::cw::lib::Bitmap> arg_dest, std::shared_ptr<::cw::lib::Bitmap> arg_wxbmp, ::cw::lib::Integer arg_x, ::cw::lib::Integer arg_y, bool arg_useMask, std::optional<std::shared_ptr<::cw::lib::Bitmap>> arg_bitsizekey);
std::tuple<std::shared_ptr<::cw::lib::Font>, std::shared_ptr<::cw::lib::Font>, std::shared_ptr<::cw::lib::Font>> _create_mfont(::cw::lib::String arg_name, ::cw::lib::Integer arg_pixels, bool arg_bold, bool arg_italic, bool arg_sys);
// 資源のコンテキストを明示して、同じ書体選択を行う。EngineやUIなしでも使える。
::cw::lib::String resolve_font_face(::cw::lib::String face,
    const std::set<::cw::lib::String>& available_faces_lower,
    const std::map<::cw::lib::String, ::cw::lib::String>& initial_fonts);
::cw::lib::String get_fontface(::cw::lib::String arg_fontface);
// 共通のRGBA画像に対するwx.Image.Rescale(..., IMAGE_QUALITY_BILINEAR)
// 相当の処理。 サンプリング規則が異なるpygameのsmoothscaleとは分ける。
std::shared_ptr<::cw::lib::Surface> resize_wx_bilinear(
    const std::shared_ptr<::cw::lib::Surface>& source,
    ::cw::lib::Integer width, ::cw::lib::Integer height);
void main();
}

