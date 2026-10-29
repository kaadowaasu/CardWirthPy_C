#pragma once

// cw/image.pyの移植。SDL/wxオブジェクトは互換ハンドルとし
// て扱い、 型付きの画像情報とRGBA画像でPythonの状態遷移を保つ。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include <span>

namespace cw::image {
class Image : public ::cw::lib::Object {
public:
    Image() = default;
    virtual ~Image() = default;
    std::shared_ptr<::cw::lib::Surface> m_image{};
    void initialize(std::shared_ptr<::cw::lib::Surface> arg_image);
    virtual std::shared_ptr<::cw::lib::Surface> get_image();
    virtual std::shared_ptr<::cw::lib::Surface> get_negaimg();
};

class ImageInfo : public ::cw::lib::Object {
public:
    ImageInfo() = default;
    virtual ~ImageInfo() = default;
    std::optional<std::shared_ptr<::cw::image::ImageInfo>> m_base{};
    std::optional<::cw::lib::String> m_basecardtype{};
    ::cw::lib::String m_path{};
    ::cw::lib::Integer m_pcnumber{};
    ::cw::lib::String m_postype{"Default"};
    void initialize(::cw::lib::String arg_path, ::cw::lib::Integer arg_pcnumber, std::optional<std::shared_ptr<::cw::image::ImageInfo>> arg_base, ::cw::lib::String arg_postype, std::optional<::cw::lib::String> arg_basecardtype);
    std::shared_ptr<::cw::image::ImageInfo> copy();
    void set_attr(std::shared_ptr<::cw::data::CWPyElement> arg_e);
    ::cw::lib::Rect calc_basecardposition(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_params, bool arg_noscale=false, std::optional<::cw::lib::String> arg_basecardtype=std::nullopt, std::optional<::cw::lib::String> arg_cardpostype=std::nullopt);
    ::cw::lib::Rect calc_basecardposition_wx(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_imgsize, bool arg_noscale=false, std::optional<::cw::lib::String> arg_basecardtype=std::nullopt, std::optional<::cw::lib::String> arg_cardpostype=std::nullopt);
    ::cw::lib::Rect _calc_basecardposition_impl(::cw::lib::Integer arg_imgwidth, ::cw::lib::Integer arg_imgheight, bool arg_noscale, std::optional<::cw::lib::String> arg_basecardtype, std::optional<::cw::lib::String> arg_cardpostype, ::cw::lib::Real arg_scale);
    bool equals(::cw::lib::Value arg_other);
    bool not_equals(::cw::lib::Value arg_other);
    ::cw::lib::String to_string();
};

class CardImage : public ::cw::image::Image {
public:
    CardImage() = default;
    virtual ~CardImage();
    inline static std::shared_ptr<::cw::lib::Object> s_rect{};
    ::cw::lib::String m__animated_owner{};
    std::set<::cw::lib::String> m__animated_paths{};
    std::optional<std::shared_ptr<::cw::lib::Surface>> m__bmp{};
    ::cw::lib::Value m__upwin{};
    std::optional<std::shared_ptr<::cw::lib::Bitmap>> m__wxbmp{};
    std::map<::cw::lib::String, ::cw::lib::Integer> m_animated_frame_ids{};
    std::map<::cw::lib::String, ::cw::lib::Integer> m_animated_instance_ids{};
    bool m_animated_paths_initialized{};
    ::cw::lib::Value m_anotherscenariocard{};
    ::cw::lib::Value m_bgtype{};
    ::cw::lib::Value m_can_loaded_scaledimage{};
    std::optional<std::vector<bool>> m_can_loaded_scaledimage_upd{};
    ::cw::lib::Value m_card_size{};
    ::cw::lib::Value m_cardbg{};
    std::map<::cw::lib::String, ::cw::lib::Real> m_image_mtime{};
    ::cw::lib::Value m_is_scenariocard{};
    ::cw::lib::Value m_name{};
    ::cw::lib::Value m_paths{};
    std::optional<std::vector<std::shared_ptr<::cw::image::ImageInfo>>> m_paths_upd{};
    ::cw::lib::Value m_premium{};
    ::cw::lib::Value m_rect{};
    ::cw::lib::Value m_scedir{};
    bool m_use_excache{};
    void initialize(std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_paths, ::cw::lib::String arg_bgtype, ::cw::lib::String arg_name, ::cw::lib::String arg_premium, ::cw::lib::Value arg_can_loaded_scaledimage, bool arg_is_scenariocard, ::cw::lib::String arg_scedir, bool arg_anotherscenariocard, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_card_size);
    virtual void update_scale();
    void set_size(::cw::lib::Integer arg_width, ::cw::lib::Integer arg_height);
    // メニューカードにも他のカードと同じフォント・倍率の規則を使う。
    // この印はソース互換用に残し、OS固有の引き伸ばしには使わない。
    void set_menu_card_text(bool arg_enabled);
    // GUIを使わない画像の契約テストからも利用できる、共通のフォント処理。
    virtual std::shared_ptr<::cw::imageretouch::Font> card_text_font(
        const ::cw::lib::String& role, ::cw::lib::Real output_scale) const;
    virtual bool smooth_card_name() const;
    virtual bool border_card_name() const;
    void clear_cache();
    void set_name(::cw::lib::String name);
    const auto& image_paths() const { return m_paths_typed; }
    const auto& image_scale_permissions() const { return m_can_loaded_scaledimage_typed; }
    std::tuple<::cw::lib::String, bool> _resolve_imagepath(std::shared_ptr<::cw::image::ImageInfo> arg_info);
    std::shared_ptr<::cw::lib::Surface> _load_pygame_image(::cw::lib::String arg_path, bool arg_mask, bool arg_can_loaded_scaledimage);
    std::shared_ptr<::cw::lib::Bitmap> _load_wxbmp_image(::cw::lib::String arg_path, bool arg_mask, bool arg_can_loaded_scaledimage);
    std::set<::cw::lib::String> get_animated_paths();
    void update_animated_paths();
    void release_animated_paths();
    void py_del();
    virtual bool update_animation();
    virtual void fix_pcimage_updated();
    ::cw::lib::Value _upwinmemo();
    std::shared_ptr<::cw::lib::Bitmap> get_wxcardbg();
    std::shared_ptr<::cw::lib::Object> get_wxrect();
    bool is_modifiedfile();
    std::shared_ptr<::cw::lib::Surface> get_image() override;
    virtual std::shared_ptr<::cw::lib::Surface> get_cardimg(std::shared_ptr<::cw::header::CardHeader> arg_header);
    std::shared_ptr<::cw::lib::Surface> get_negaimg() override;
    std::shared_ptr<::cw::lib::Surface> get_clickedimg(std::optional<std::shared_ptr<::cw::lib::Object>> arg_rect, std::optional<std::shared_ptr<::cw::lib::Surface>> arg_image);
    virtual std::shared_ptr<::cw::lib::Bitmap> get_wxbmp();
    std::shared_ptr<::cw::lib::Bitmap> get_cardwxbmp(::cw::lib::Value arg_header, std::optional<std::shared_ptr<::cw::character::Character>> arg_test_aptitude);
    std::shared_ptr<::cw::lib::Bitmap> get_wxnegabmp();
    std::shared_ptr<::cw::lib::Bitmap> get_wxclickedbmp(::cw::lib::Value arg_header, std::shared_ptr<::cw::lib::Bitmap> arg_wxbmp, std::optional<std::shared_ptr<::cw::sprite::card::PlayerCard>> arg_test_aptitude);
    static std::shared_ptr<::cw::lib::Bitmap> get_wxclickedbmp_static(std::shared_ptr<::cw::lib::Bitmap> arg_wxbmp);
    std::shared_ptr<::cw::lib::Bitmap> get_wxdealingbmp(::cw::lib::Value arg_header, std::shared_ptr<::cw::lib::Bitmap> arg_wxbmp, ::cw::lib::Integer arg_n, std::optional<std::shared_ptr<::cw::character::Character>> arg_test_aptitude);
    virtual void update(std::shared_ptr<::cw::sprite::card::CWPyCard> arg_card, std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_test_aptitude);

protected:
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> m_paths_typed{};
    std::vector<bool> m_can_loaded_scaledimage_typed{};
    ::cw::lib::String m_bgtype_typed{};
    ::cw::lib::String m_name_typed{};
    ::cw::lib::String m_premium_typed{};
    ::cw::lib::String m_scedir_typed{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> m_card_size_typed{-1, -1};
    bool m_is_scenariocard_typed{};
    bool m_anotherscenariocard_typed{};
    bool m_menu_card_text_typed{};
    std::shared_ptr<::cw::lib::Surface> m_cardbg_typed{};
    ::cw::lib::Rect m_rect_typed{};
};

class CharacterCardImage : public ::cw::image::CardImage {
public:
    const auto& override_image_paths() const { return m_override_paths_typed; }
    const auto& override_image_scale_permissions() const { return m_override_flags_typed; }
    CharacterCardImage() = default;
    virtual ~CharacterCardImage() = default;
    ::cw::lib::Value m__no_statusimg{};
    ::cw::lib::Value m__pos_noscale{};
    std::vector<::cw::lib::Value> m_cardimgs{};
    ::cw::lib::Value m_ccard{};
    // 現在の画像はImage、保留中の表面変更はCardImageが所有する。 Python
    // と同じく属性はそれぞれ一つとし、派生クラスに別のスナップショットを持たせない。
    ::cw::lib::Value m_is_override_image{};
    ::cw::lib::Value m_is_override_name{};
    ::cw::lib::Value m_levelimg{};
    std::tuple<::cw::lib::Value, ::cw::lib::Value> m_levelimg_pos{};
    std::shared_ptr<::cw::lib::Surface> m_lifeimg{};
    std::optional<std::shared_ptr<::cw::lib::Surface>> m_nameimg{};
    ::cw::lib::Value m_override_images{};
    std::optional<std::tuple<std::vector<std::shared_ptr<::cw::image::ImageInfo>>, std::vector<bool>>> m_override_images_upd{};
    ::cw::lib::Value m_override_name{};
    void initialize(::cw::lib::Value arg_ccard, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos_noscale, bool arg_can_loaded_scaledimage, bool arg_is_scenariocard, ::cw::lib::String arg_scedir, bool arg_is_override_name, ::cw::lib::String arg_override_name, bool arg_is_override_image, std::optional<std::tuple<std::vector<std::shared_ptr<::cw::image::ImageInfo>>, std::vector<bool>>> arg_override_images, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_card_size, bool arg_borrowed_owner = false);
    void update_scale() override;
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> _bottom_left_position(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    void fix_pcimage_updated() override;
    void set_faceimgs(std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_paths, ::cw::lib::Value arg_can_loaded_scaledimage);
    bool update_animation() override;
    void set_nameimg(::cw::lib::String arg_name);
    void set_levelimg(::cw::lib::Integer arg_level);
    void update(std::shared_ptr<::cw::sprite::card::CWPyCard> arg_card, std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_test_aptitude) override;
    std::optional<std::shared_ptr<::cw::lib::Object>> update_statusimg(::cw::lib::Value arg_ccard, std::optional<bool> arg_is_runningevent);
    std::shared_ptr<::cw::lib::Surface> _put_number(std::shared_ptr<::cw::lib::Surface> arg_image, ::cw::lib::Integer arg_num, bool arg_always, std::optional<bool> arg_is_runningevent);
    void _put_enhanceimg(std::vector<::cw::lib::Value>& arg_seq, std::shared_ptr<::cw::lib::Surface> arg_bmp, ::cw::lib::Integer arg_value, ::cw::lib::Integer arg_duration, std::optional<bool> arg_is_runningevent);
    ::cw::lib::String get_cardbgname(std::shared_ptr<::cw::character::Character> arg_ccard);
    std::shared_ptr<::cw::lib::Surface> get_image() override;
    std::shared_ptr<::cw::lib::Surface> get_cardimg(std::shared_ptr<::cw::header::CardHeader> arg_header) override;

private:
    std::weak_ptr<::cw::character::Character> m_ccard_typed{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> m_pos_noscale_typed{0, 0};
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> m_override_paths_typed{};
    std::vector<bool> m_override_flags_typed{};
    std::vector<std::shared_ptr<::cw::lib::Surface>> m_cardimgs_typed{};
    std::shared_ptr<::cw::lib::Surface> m_no_statusimg_typed{};
    std::shared_ptr<::cw::lib::Surface> m_levelimg_typed{};
    std::shared_ptr<::cw::lib::Surface> m_nameimg_typed{};
};

class LargeCardImage : public ::cw::image::CardImage {
public:
    LargeCardImage() = default;
    virtual ~LargeCardImage() = default;
    void initialize(std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_paths, ::cw::lib::String arg_bgtype, ::cw::lib::String arg_name, ::cw::lib::String arg_premium, ::cw::lib::Value arg_can_loaded_scaledimage, bool arg_is_scenariocard, ::cw::lib::String arg_scedir, bool arg_anotherscenariocard, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_card_size);
    std::shared_ptr<::cw::lib::Surface> get_image() override;
    std::shared_ptr<::cw::lib::Bitmap> get_wxbmp() override;
};

std::vector<std::shared_ptr<::cw::image::ImageInfo>> get_imageinfos(std::shared_ptr<::cw::data::CWPyElement> arg_data, bool arg_pcnumber);
std::vector<std::shared_ptr<::cw::image::ImageInfo>> get_imageinfos_p(std::shared_ptr<::cw::header::GetProperty> arg_prop, bool arg_pcnumber);
// cw/image.pyと同じ手順でキャラクターカードの体力バ
// ーを作る。 任意のマスクがあればLIFEGUAGE2を使い、
// なければ従来のLIFEGUAGEのカラーキー付き枠を使う。
std::shared_ptr<::cw::lib::Surface> compose_lifebar(
    const std::shared_ptr<::cw::lib::Surface>& arg_lifebar,
    const std::shared_ptr<::cw::lib::Surface>& arg_guage,
    const std::shared_ptr<::cw::lib::Surface>& arg_lifemask,
    ::cw::lib::Integer arg_barx, ::cw::lib::Integer arg_bary);
std::shared_ptr<::cw::lib::Surface> create_type2textcell(::cw::lib::String arg_text, ::cw::lib::String arg_face, ::cw::lib::Integer arg_size, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_color, bool arg_bold, bool arg_italic, bool arg_uline, bool arg_sline, bool arg_vertical, bool arg_antialias, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_cellsize, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_bcolor, ::cw::lib::Integer arg_bwidth);
void draw_textcell(std::shared_ptr<::cw::lib::Surface> arg_image, std::shared_ptr<::cw::lib::Object> arg_rect, ::cw::lib::String arg_text, ::cw::lib::String arg_face, ::cw::lib::Integer arg_size, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_color, bool arg_bold, bool arg_italic, bool arg_uline, bool arg_sline, bool arg_vertical, bool arg_antialias, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>> arg_bcolor);
std::tuple<std::shared_ptr<::cw::imageretouch::Font>, ::cw::lib::Integer> get_textcellfont(::cw::lib::Integer arg_size, ::cw::lib::String arg_face, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_color, bool arg_bold, bool arg_italic, bool arg_uline, bool arg_vertical, bool arg_antialiased);
std::shared_ptr<::cw::lib::Surface> create_colorcell(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_color1, ::cw::lib::String arg_gradient, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_color2);
::cw::lib::Integer create_colorcell_calc_per(::cw::lib::Integer arg_mn, ::cw::lib::Integer arg_mx, ::cw::lib::Integer arg_per);
std::shared_ptr<::cw::lib::Surface> zoomcard(std::shared_ptr<::cw::lib::Surface> arg_image, ::cw::lib::Real arg_scale);
// 共通の最近傍拡縮処理。pygame.transform.scaleに対応する。
std::shared_ptr<::cw::lib::Surface> scale_nearest(std::shared_ptr<::cw::lib::Surface> arg_surface, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size);
std::shared_ptr<::cw::lib::Surface> smoothscale_card(std::shared_ptr<::cw::lib::Surface> arg_surface, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size, bool arg_smoothing);
// クリック可能な画像で共用する、pygame.transform.rotozoom(surface, 0, scale)相当の処理。
std::shared_ptr<::cw::lib::Surface> rotozoom_zero(std::shared_ptr<::cw::lib::Surface> surface, ::cw::lib::Real scale);
std::shared_ptr<::cw::lib::Surface> smoothscale(std::shared_ptr<::cw::lib::Surface> arg_surface, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size, bool arg_smoothing, bool arg_iscard);
std::tuple<::cw::lib::Value, bool> fix_cwnext16bitbitmap(::cw::lib::Bytes arg_data);
std::tuple<::cw::lib::Bytes, bool> fix_cwnext32bitbitmap(::cw::lib::Bytes arg_data);
::cw::lib::Bytes patch_rle4bitmap(::cw::lib::Bytes arg_data);
std::tuple<::cw::lib::Bytes, bool> patch_rle8bitmap(::cw::lib::Bytes arg_data);
// 借用したネイティブデータから、Pyと同じヘッダー解析を行う。ファイル全体はコピーしない。
::cw::lib::Integer get_bmpdepth_view(std::span<const std::uint8_t> arg_data);
::cw::lib::Integer get_bmpdepth(::cw::lib::Bytes arg_data);
std::optional<::cw::lib::Integer> get_bicompression(::cw::lib::Bytes arg_data);
bool has_pngalpha(::cw::lib::Bytes arg_data);
// pygame形式の通常の共通blit。論理RGBの不透明度とカラーキーも扱う。
void blit_surface_pixels(const std::shared_ptr<::cw::lib::Surface>& destination,
                         const std::shared_ptr<::cw::lib::Surface>& source,
                         ::cw::lib::Integer x, ::cw::lib::Integer y,
                         std::optional<::cw::lib::Rect> destination_clip = std::nullopt);
// 両方のテキストセル処理で使う、Pythonのstr.splitlines()相当のUTF-8分割。
std::vector<::cw::lib::String> split_lines(const ::cw::lib::String& text);
void main();
}

