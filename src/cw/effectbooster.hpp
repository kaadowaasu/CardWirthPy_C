#pragma once

// cw/effectbooster.pyの移植。設定と動作状態は型付きフィールドへ
// 保持し、 描画・イベントの所有関係はEffectBoosterBackend経由
// で公開する。 移植可能なSurface・画像処理は単独でも実行できるようにする。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include "cw/image.hpp"

#include <exception>

namespace cw::effectbooster {
inline constexpr const char* click_wait_notice = "クリックor(右クリックor長時間タッチ)で進みます。";
inline constexpr ::cw::lib::Integer jpdc_default_copy_mode = 2;
using AnimatedSurfaceLoader = std::function<std::tuple<
    std::optional<std::shared_ptr<::cw::lib::Surface>>, ::cw::lib::Integer>(
        const ::cw::lib::String&)>;

class AnimationCounter : public ::cw::lib::Object {
public:
    AnimationCounter() = default;
    virtual ~AnimationCounter() = default;
    bool m_all_cut{};
    ::cw::lib::Integer m_count{};
    ::cw::lib::Integer m_skip_count{};
    ::cw::lib::Integer m_time_elapsed{};
    void initialize();
    virtual std::shared_ptr<::cw::effectbooster::AnimationCounter> get_reloadcounter();
    virtual bool countup();
};

// EffectBooster の実行境界。Surface加工・設定/XML/cacheは常にこのファイル内の
// portable 実装を通り、イベント・音声・画面合成だけを任意のSDL/Windows層へ渡す。
// 未登録時も make_default_backend() の software/Windows 経路が自動利用される。
struct EffectBoosterBackend {
    using SurfacePtr = std::shared_ptr<::cw::lib::Surface>;
    std::function<void(::cw::lib::Integer, std::shared_ptr<::cw::effectbooster::AnimationCounter>)> wait{};
    std::function<void(const ::cw::lib::String&, bool)> play_sound{};
    std::function<SurfacePtr()> capture_background{};
    std::function<void(SurfacePtr&)> compose_temporal{};
    std::function<void(const ::cw::lib::String&)> set_title{};
};

void set_backend(std::shared_ptr<EffectBoosterBackend> arg_backend);
std::shared_ptr<EffectBoosterBackend> make_default_backend();

class EffectBoosterConfig : public ::cw::lib::Object {
public:
    EffectBoosterConfig() = default;
    virtual ~EffectBoosterConfig() = default;
    std::vector<::cw::lib::Value> m__orderedsecs{};
    ::cw::lib::ValueMap m__sections{};
    ::cw::lib::Integer m_dirdepth{};
    ::cw::lib::Value m_path{};
    using Section = std::map<::cw::lib::String, ::cw::lib::String>;
    std::vector<::cw::lib::String> m_orderedsecs_typed{};
    std::map<::cw::lib::String, Section> m_sections_typed{};
    ::cw::lib::String m_path_typed{};
    bool m_section_ignorecase_typed{true};
    void initialize(::cw::lib::String arg_path, ::cw::lib::String arg_firstsection, bool arg_section_ignorecase);
    std::vector<::cw::lib::String> sections();
    bool has_section(::cw::lib::String arg_name);
    ::cw::lib::String get(::cw::lib::String arg_section, ::cw::lib::String arg_option, ::cw::lib::String arg_py_default);
    std::optional<::cw::lib::String> get_2(::cw::lib::String arg_section, ::cw::lib::String arg_option, ::cw::lib::Value arg_py_default = {});
    std::optional<::cw::lib::String> get_3(::cw::lib::String arg_section, ::cw::lib::String arg_option, std::optional<::cw::lib::String> arg_py_default = std::nullopt);
    ::cw::lib::Integer get_int(::cw::lib::String arg_section, ::cw::lib::String arg_option, ::cw::lib::Integer arg_py_default);
    std::optional<::cw::lib::Integer> get_int_2(::cw::lib::String arg_section, ::cw::lib::String arg_option, ::cw::lib::Value arg_py_default);
    std::optional<::cw::lib::Integer> get_int_3(::cw::lib::String arg_section, ::cw::lib::String arg_option, std::optional<::cw::lib::Integer> arg_py_default);
    bool get_bool(::cw::lib::String arg_section, ::cw::lib::String arg_option, bool arg_py_default);
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> get_color(::cw::lib::String arg_section, ::cw::lib::String arg_option, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_py_default);
    std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>> get_color_2(::cw::lib::String arg_section, ::cw::lib::String arg_option, ::cw::lib::Value arg_py_default = {});
    std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>> get_color_3(::cw::lib::String arg_section, ::cw::lib::String arg_option, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>> arg_py_default = std::nullopt);
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> get_ints(::cw::lib::String arg_section, ::cw::lib::String arg_option, ::cw::lib::Value arg_length, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_py_default);
    std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> get_ints_2(::cw::lib::String arg_section, ::cw::lib::String arg_option, ::cw::lib::Value arg_length, ::cw::lib::Value arg_py_default);
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> get_ints_3(::cw::lib::String arg_section, ::cw::lib::String arg_option, ::cw::lib::Value arg_length, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_py_default);
    std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>> get_ints_4(::cw::lib::String arg_section, ::cw::lib::String arg_option, ::cw::lib::Value arg_length, ::cw::lib::Value arg_py_default);
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> get_ints_5(::cw::lib::String arg_section, ::cw::lib::String arg_option, ::cw::lib::Value arg_length, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_py_default);
    std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>> get_ints_6(::cw::lib::String arg_section, ::cw::lib::String arg_option, ::cw::lib::Value arg_length, ::cw::lib::Value arg_py_default);
    std::optional<std::vector<::cw::lib::Integer>> get_ints_7(::cw::lib::String arg_section, ::cw::lib::String arg_option, ::cw::lib::Integer arg_length, std::optional<std::vector<::cw::lib::Integer>> arg_py_default);
};


class JpyCache : public ::cw::lib::Object {
public:
    JpyCache() = default;
    virtual ~JpyCache() = default;
    std::optional<std::shared_ptr<::cw::lib::Surface>> m_before{};
    std::optional<std::shared_ptr<::cw::lib::Surface>> m_beforeback{};
    std::optional<std::shared_ptr<::cw::lib::Object>> m_beforerect{};
    std::map<::cw::lib::Integer, std::shared_ptr<::cw::lib::Surface>> m_img{};
    std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> m_pos_noscale{};
    std::optional<::cw::lib::Rect> m_beforerect_typed{};
    std::map<::cw::lib::Integer, std::shared_ptr<::cw::lib::Surface>> m_img_typed{};
    std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> m_pos_noscale_typed{};
    void initialize();
    void restore();
    void save_position_noscale(std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos_noscale);
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> load_position_noscale();
    void save_image(::cw::lib::Integer arg_n, std::shared_ptr<::cw::lib::Surface> arg_image);
    std::shared_ptr<::cw::lib::Surface> load_image(::cw::lib::Integer arg_n);
};

class ScreenRescale : public ::cw::lib::Object, public std::exception {
public:
    ScreenRescale() = default;
    virtual ~ScreenRescale() = default;
    const char* what() const noexcept { return "effect booster requires screen rescale"; }
};

class CutAnimation : public ::cw::effectbooster::AnimationCounter {
public:
    CutAnimation() = default;
    virtual ~CutAnimation() = default;
    void initialize();
    std::shared_ptr<::cw::effectbooster::AnimationCounter> get_reloadcounter() override;
    bool countup() override;
};

class JpdcImage : public ::cw::image::Image {
public:
    JpdcImage() = default;
    virtual ~JpdcImage() = default;
    ::cw::lib::Value m_image{};
    ::cw::lib::Rect m_clip_typed{};
    ::cw::lib::Integer m_copymode_typed{};
    ::cw::lib::String m_path_typed{};
    void initialize(bool arg_mask, ::cw::lib::String arg_path, std::optional<std::shared_ptr<::cw::effectbooster::JpyCache>> arg_cache = std::nullopt, ::cw::lib::Integer arg_defaultcopymode = jpdc_default_copy_mode, std::optional<std::shared_ptr<::cw::effectbooster::AnimationCounter>> arg_doanime = std::nullopt);
    void wait(std::shared_ptr<::cw::effectbooster::AnimationCounter> arg_doanime);
};

class JptxImage : public ::cw::image::Image {
public:
    JptxImage() = default;
    virtual ~JptxImage() = default;
    std::vector<::cw::lib::String> m_chars{};
    std::shared_ptr<::cw::imageretouch::Font> m_font{};
    std::shared_ptr<::cw::imageretouch::Font> m_font2{};
    std::shared_ptr<::cw::imageretouch::Font> m_font2_noscale{};
    std::shared_ptr<::cw::imageretouch::Font> m_font_noscale{};
    ::cw::lib::Value m_fontcolor{};
    ::cw::lib::Value m_fontface{};
    ::cw::lib::Value m_fontpixels{};
    ::cw::lib::Value m_fontpixels_noscale{};
    ::cw::lib::Integer m_h{};
    ::cw::lib::Value m_image{};
    ::cw::lib::Value m_lineheight{};
    bool m_nolinedata{};
    std::vector<std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>>> m_oldfonts{};
    ::cw::lib::Value m_outer{};
    bool m_strike{};
    ::cw::lib::String m_tag{};
    bool m_tagonly{};
    ::cw::lib::Integer m_w{};
    ::cw::lib::Integer m_x{};
    ::cw::lib::Real m_y{};
    ::cw::lib::Integer m_width_typed{};
    ::cw::lib::Integer m_height_typed{};
    bool m_mask_typed{};
    void initialize(::cw::lib::String arg_path, bool arg_mask);
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> get_fontcolor(::cw::lib::String arg_fontcolor, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> arg_py_default);
    std::tuple<bool, ::cw::lib::String, std::map<::cw::lib::String, ::cw::lib::String>> parse_tag(::cw::lib::String arg_tag);
};

class JpyImage : public ::cw::image::Image {
public:
    JpyImage() = default;
    virtual ~JpyImage() = default;
    ::cw::lib::Value m_image{};
    bool m_is_animated{};
    bool m_is_cacheable{};
    ::cw::lib::String m_path_typed{};
    std::set<::cw::lib::String> m_animated_paths_typed{};
    ::cw::effectbooster::AnimatedSurfaceLoader m_animated_surface_loader{};
    void initialize(::cw::lib::String arg_path, bool arg_mask, std::optional<std::shared_ptr<::cw::effectbooster::JpyCache>> arg_cache, std::optional<std::shared_ptr<::cw::effectbooster::AnimationCounter>> arg_doanime, std::optional<std::shared_ptr<::cw::effectbooster::_JpySubImage>> arg_parent, ::cw::effectbooster::AnimatedSurfaceLoader arg_animated_surface_loader = {});
};

class _JpySubImage : public ::cw::image::Image {
public:
    _JpySubImage() = default;
    virtual ~_JpySubImage() = default;
    inline static std::shared_ptr<::cw::lib::Surface> s_image{};
    inline static std::tuple<::cw::lib::Integer, ::cw::lib::Integer> s_position{};
    inline static std::tuple<::cw::lib::Integer, ::cw::lib::Integer> s_position_noscale{};
    inline static ::cw::lib::Integer s_savecache{};
    inline static bool s_visible{};
    ::cw::lib::Value m_alpha{};
    ::cw::lib::Value m_animation{};
    ::cw::lib::Value m_animeclip{};
    ::cw::lib::Value m_animemove{};
    ::cw::lib::Value m_animemove_noscale{};
    ::cw::lib::Value m_animeposition{};
    std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> m_animeposition_noscale{};
    ::cw::lib::Value m_animespeed{};
    ::cw::lib::Value m_cache{};
    bool m_can_mask{};
    ::cw::lib::Value m_clip{};
    ::cw::lib::Value m_colormap{};
    ::cw::lib::Value m_configdepth{};
    ::cw::lib::Value m_configpath{};
    ::cw::lib::Integer m_defaultcopymode{};
    ::cw::lib::Value m_dirtype{};
    ::cw::lib::Value m_exchange{};
    ::cw::lib::Value m_filename{};
    ::cw::lib::Value m_filter{};
    ::cw::lib::Value m_flip{};
    ::cw::lib::Value m_image{};
    bool m_is_animated{};
    bool m_is_cacheable{};
    ::cw::lib::Value m_loadcache{};
    ::cw::lib::Value m_mask{};
    ::cw::lib::Value m_mirror{};
    ::cw::lib::Value m_noise{};
    ::cw::lib::Value m_noisepoint{};
    ::cw::lib::Value m_paintmode{};
    ::cw::lib::Value m_smooth{};
    ::cw::lib::Value m_starttick{};
    ::cw::lib::Value m_turn{};
    ::cw::lib::Value m_waittime{};
    // Pythonの属性に対応する型付きの保持先。上のValueメンバーは生成済み
    // ABIとの互換用に残し、 Noneと既定値の状態はこれらのフィールドで扱う。
    ::cw::lib::String m_configpath_typed{};
    ::cw::lib::Integer m_configdepth_typed{};
    ::cw::lib::Integer m_dirtype_typed{1};
    ::cw::lib::String m_filename_typed{};
    bool m_smooth_typed{};
    std::optional<::cw::lib::Rect> m_clip_typed{};
    ::cw::lib::Integer m_loadcache_typed{};
    bool m_flip_typed{};
    bool m_mirror_typed{};
    ::cw::lib::Integer m_turn_typed{};
    ::cw::lib::Integer m_mask_typed{};
    ::cw::lib::Integer m_colormap_typed{};
    ::cw::lib::Integer m_alpha_typed{};
    std::optional<::cw::lib::Integer> m_exchange_typed{};
    ::cw::lib::Integer m_noise_typed{};
    ::cw::lib::Integer m_noisepoint_typed{};
    ::cw::lib::Integer m_filter_typed{};
    ::cw::lib::Integer m_waittime_typed{};
    ::cw::lib::Integer m_animation_typed{};
    std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> m_animemove_noscale_typed{};
    std::optional<::cw::lib::Rect> m_animeclip_typed{};
    ::cw::lib::Integer m_animespeed_typed{};
    std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> m_animeposition_noscale_typed{};
    ::cw::lib::Integer m_paintmode_typed{};
    ::cw::lib::Integer m_defaultcopymode_typed{2};
    bool m_is_animated_typed{};
    bool m_is_cacheable_typed{true};
    bool m_can_mask_typed{true};
    std::set<::cw::lib::String> m_animated_paths_typed{};
    ::cw::effectbooster::AnimatedSurfaceLoader m_animated_surface_loader{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> m_position_noscale_typed{0,0};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> m_position_typed{0,0};
    ::cw::lib::Integer m_width_typed{-1};
    ::cw::lib::Integer m_height_typed{-1};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> m_color_typed{0,0,0};
    bool m_haswidth_typed{};
    bool m_transparent_typed{true};
    bool m_visible_typed{true};
    ::cw::lib::Integer m_savecache_typed{};
    std::chrono::steady_clock::time_point m_starttick_typed{};
    std::optional<::cw::lib::Color> m_colorkey_typed{};
    void initialize(std::shared_ptr<::cw::effectbooster::EffectBoosterConfig> arg_config, ::cw::lib::String arg_section, std::shared_ptr<::cw::effectbooster::JpyCache> arg_cache);
    void draw2back(std::shared_ptr<::cw::effectbooster::JpyBackGroundImage> arg_back, bool arg_mask);
    void drawtemp(std::shared_ptr<::cw::effectbooster::AnimationCounter> arg_doanime);
    void _drawtemp_impl(std::shared_ptr<::cw::effectbooster::AnimationCounter> arg_doanime, std::shared_ptr<::cw::lib::Surface> arg_background, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos, bool arg_redraw, bool arg_anime, std::optional<::cw::lib::Integer> arg_waittime, bool arg_nowait);
    std::shared_ptr<::cw::lib::Surface> clip_tempimg(std::shared_ptr<::cw::lib::Surface> arg_image, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos);
    void wait(std::shared_ptr<::cw::effectbooster::AnimationCounter> arg_doanime, bool arg_anime, std::optional<::cw::lib::Integer> arg_waittime);
    ::cw::lib::Integer _cut_waittime(::cw::lib::Integer arg_waittime);
    void retouch();
    void load(std::shared_ptr<::cw::effectbooster::AnimationCounter> arg_doanime);
    std::tuple<::cw::lib::String, bool> get_filepath(::cw::lib::Integer arg_dirtype);
};

class JpyBackGroundImage : public ::cw::effectbooster::_JpySubImage {
public:
    JpyBackGroundImage() = default;
    virtual ~JpyBackGroundImage() = default;
    ::cw::lib::Value m_backcolor{};
    ::cw::lib::Value m_configdepth{};
    ::cw::lib::String m_configpath{};
    ::cw::lib::Value m_dirdepth{};
    ::cw::lib::Value m_height{};
    ::cw::lib::Value m_position{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> m_position_noscale{};
    ::cw::lib::Integer m_savecache{};
    ::cw::lib::Value m_transparent{};
    bool m_vanish_anime{};
    bool m_visible{};
    ::cw::lib::Value m_width{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> m_backcolor_typed{0,0,0};
    ::cw::lib::Integer m_width_bg_typed{-1};
    ::cw::lib::Integer m_height_bg_typed{-1};
    bool m_vanish_anime_typed{};
    bool m_transparent_bg_typed{};
    ::cw::lib::Integer m_dirdepth_typed{};
    ::cw::lib::String m_configpath_bg_typed{};
    void initialize(std::shared_ptr<::cw::effectbooster::EffectBoosterConfig> arg_config, std::shared_ptr<::cw::effectbooster::JpyCache> arg_cache, bool arg_mask);
};

class JpyPartsImage : public ::cw::effectbooster::_JpySubImage {
public:
    JpyPartsImage() = default;
    virtual ~JpyPartsImage() = default;
    ::cw::lib::Value m_color{};
    bool m_haswidth{};
    ::cw::lib::Value m_height{};
    ::cw::lib::Value m_position{};
    ::cw::lib::Value m_position_noscale{};
    ::cw::lib::Value m_savecache{};
    ::cw::lib::Value m_transparent{};
    ::cw::lib::Value m_visible{};
    ::cw::lib::Value m_width{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> m_color_typed{0,0,0};
    ::cw::lib::Integer m_width_parts_typed{-1};
    ::cw::lib::Integer m_height_parts_typed{-1};
    bool m_haswidth_parts_typed{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> m_position_parts_noscale_typed{0,0};
    bool m_transparent_parts_typed{true};
    bool m_visible_parts_typed{true};
    ::cw::lib::Integer m_savecache_parts_typed{};
    void initialize(std::shared_ptr<::cw::effectbooster::EffectBoosterConfig> arg_config, ::cw::lib::String arg_section, std::shared_ptr<::cw::effectbooster::JpyCache> arg_cache, bool arg_mask);
};

void wait_effectbooster(::cw::lib::Integer arg_waittime, std::shared_ptr<::cw::effectbooster::AnimationCounter> arg_doanime);
std::tuple<::cw::lib::String, bool> get_filepath_s(::cw::lib::String arg_configpath, ::cw::lib::Integer arg_dirdepth, ::cw::lib::String arg_filename, ::cw::lib::Integer arg_dirtype, ::cw::lib::String arg_scedir);
::cw::lib::String get_filepath_s_find_materialpath(::cw::lib::String arg_fpath);
void main();
}

