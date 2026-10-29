#pragma once

// cw/sprite/background.py の移植。セル
// ・状態処理を SDL 描画器から独立させ、描画器への受け渡し
// は background.cpp の各転送境界に明示する。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include "cw/sprite/base.hpp"
#include "cw/sprite/card.hpp"

namespace cw::sprite::message {
class NameListItem;
}

namespace cw::sprite::background {

class BackGround;

// 背景描画は一つの共通アダプターを通す。
// ホストは音声などのサービスを注入できる
// が、描画や操作の実装を置き換えない。
class BackgroundUiBackend : public ::cw::lib::Object {
public:
    virtual ~BackgroundUiBackend() = default;
    virtual void bind(BackGround& arg_background) = 0;
    virtual void invalidate(BackGround& arg_background, std::optional<::cw::lib::Rect> arg_rect) = 0;
    virtual void redraw(BackGround& arg_background) = 0;
    virtual void add_sprite(std::shared_ptr<::cw::lib::Object> arg_group,
                            std::shared_ptr<::cw::sprite::base::CWPySprite> arg_sprite,
                            ::cw::lib::Integer arg_layer) = 0;
    virtual void remove_sprite(std::shared_ptr<::cw::lib::Object> arg_group,
                               std::shared_ptr<::cw::sprite::base::CWPySprite> arg_sprite) = 0;
    virtual bool decode_effect_image(::cw::lib::String arg_path, bool arg_mask,
                                     std::shared_ptr<::cw::lib::Surface>& arg_image) = 0;
    virtual void play_audio(::cw::lib::String arg_path, bool arg_loop) = 0;
};

std::shared_ptr<BackgroundUiBackend> make_background_ui_backend();
std::shared_ptr<BackgroundUiBackend> background_ui_backend();
// 保存された背景 Surface のキーだけを分類し、通常のファイルキーは対象にしない。
bool is_jpy_surface_cache_key(const ::cw::lib::String& key);
void remove_background_proxies_for_owner(
    const ::cw::sprite::base::CWPySprite* arg_owner) noexcept;

// Python版の背景セルtupleに共通する末尾フィールドを型付きで保持する。
// comparison_keyは診断用の派生文字列。セルの同一性は型付きフィールドで比較する。
struct BackgroundCellData : public ::cw::lib::Object {
    ::cw::lib::Integer type{};
    ::cw::lib::String path{};
    ::cw::lib::String comparison_key{};
    ::cw::lib::Point pos{};
    ::cw::lib::Size size{};
    ::cw::lib::String flag{};
    bool visible{};
    ::cw::lib::Integer layer{};
    ::cw::lib::String cellname{};
    bool battle{};
    // セル種別ごとのデータを型付きで保持する。Python の tuple
    // を不透明な Value にまとめず、再読み込み時の比較を保つ。
    bool inusecard{};
    bool scaledimage{};
    bool mask{};
    ::cw::lib::String smoothing{};
    ::cw::lib::String blend{};
    ::cw::lib::String gradient{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> color1{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> color2{};
    ::cw::lib::String text{};
    ::cw::lib::String face{};
    ::cw::lib::Integer text_size{};
    ::cw::lib::Integer border_width{};
    ::cw::lib::String border_type{};
    bool bold{};
    bool italic{};
    bool underline{};
    bool strike{};
    bool vertical{};
    bool antialias{};
    bool loaded{};
    ::cw::lib::String update_type{};
    ::cw::lib::Integer pcnumber{};
    bool expand{};
    ::cw::lib::String scenario{};
    ::cw::lib::String author{};
    ::cw::lib::String names_key{};
    std::vector<std::shared_ptr<::cw::sprite::message::NameListItem>> names{};
    // Python の None と、空リスト・空のシナリオ名と作者名の組を区別して保つ。
    bool names_present{};
    bool scenarioinfo_present{};
};

class BackGround : public ::cw::sprite::base::CWPySprite,
                   public std::enable_shared_from_this<BackGround> {
public:
    BackGround() = default;
    virtual ~BackGround() = default;
    std::map<::cw::lib::String, ::cw::lib::Integer> m__animated_load_counts{};
    ::cw::lib::String m__animated_owner{};
    std::map<std::tuple<::cw::lib::String, ::cw::lib::Integer>, ::cw::lib::String> m__animated_owners{};
    std::set<std::tuple<::cw::lib::String, ::cw::lib::Integer>> m__animated_paths{};
    std::map<std::tuple<::cw::lib::String, ::cw::lib::Integer>, std::set<::cw::lib::String>> m_effect_animated_paths{};
    std::vector<std::tuple<::cw::lib::Integer, std::optional<::cw::lib::Value>>> m__bgs{};
    std::vector<std::shared_ptr<::cw::sprite::background::Curtain>> m__curtains{};
    ::cw::lib::Value m__doanime{};
    std::vector<std::shared_ptr<::cw::data::CWPyElement>> m__elements{};
    bool m__force_noinhrt{};
    bool m__in_playing{};
    ::cw::lib::Integer m__inhrt_index{};
    bool m__init{};
    std::tuple<::cw::lib::String, ::cw::lib::Value> m__ttype{};
    std::map<std::tuple<::cw::lib::String, ::cw::lib::Integer>, ::cw::lib::Integer> m_animated_frame_ids{};
    std::map<std::tuple<::cw::lib::String, ::cw::lib::Integer>, ::cw::lib::Integer> m_animated_instance_ids{};
    std::map<std::tuple<::cw::lib::String, ::cw::lib::Integer>, std::shared_ptr<::cw::lib::Surface>> m_animated_surfaces{};
    std::vector<std::tuple<::cw::lib::Integer, std::optional<::cw::lib::Value>>> m_bgs{};
    bool m_curtain_all{};
    bool m_curtain_layers_shifted{};
    bool m_curtained{};
    std::vector<std::tuple<::cw::lib::Integer, ::cw::lib::Value, ::cw::lib::String, ::cw::lib::Integer>> m_foregroundlist{};
    std::set<std::shared_ptr<::cw::sprite::background::BgCell>> m_foregrounds{};
    bool m_has_jpdcimage{};
    // 合成済み画像、矩形、レイヤーは CWPySprite に属する。ここには UI
    // ・リソースのキャッシュだけを置き、Python の属性の第二の複製を持たない。
    std::map<::cw::lib::Integer, std::tuple<std::vector<std::tuple<::cw::lib::String, std::shared_ptr<::cw::image::ImageInfo>>>, bool>> m_pc_cache{};
    bool m_reload_jpdcimage{};
    bool m_use_excache{};
    // このスプライト内の、Python の resource_cache/background_image_mtime
    // 相当の状態。ヘッドレス起動ではシナリオオブジェクトがない場合がある。
    std::map<::cw::lib::String, std::shared_ptr<::cw::lib::Surface>> m_surface_cache{};
    std::map<::cw::lib::String, std::tuple<::cw::lib::String, ::cw::lib::Real>> m_stored_filepaths{};
    std::shared_ptr<BackgroundUiBackend> m_ui_backend{};
    std::shared_ptr<::cw::lib::Surface> m_ui_surface{};
    std::vector<::cw::lib::Rect> m_ui_dirty_rects{};
    bool m_ui_dirty{true};
    void initialize();
    void bind_ui_backend();
    void invalidate_ui(std::optional<::cw::lib::Rect> arg_rect = std::nullopt);
    void redraw_ui();
    bool is_initialized();
    void init();
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
    std::tuple<::cw::lib::String, bool> _resolve_imagecell_path(::cw::lib::Value arg_d);
    bool _update_animated_images();
    std::map<std::tuple<::cw::lib::String, ::cw::lib::Integer>, ::cw::lib::String> _get_animated_paths();
    std::tuple<::cw::lib::String, ::cw::lib::Integer> _next_animated_cell_key(::cw::lib::String arg_path);
    ::cw::lib::String _get_animated_owner(std::tuple<::cw::lib::String, ::cw::lib::Integer> arg_cellkey);
    std::tuple<::cw::lib::String, ::cw::lib::Integer> _get_effect_animated_key(std::tuple<::cw::lib::String, ::cw::lib::Integer> arg_cellkey, const ::cw::lib::String& arg_path);
    void _sync_animated_paths();
    void release_animated_paths();
    void update_scale();
    void update_skin(::cw::lib::String arg_oldskindir, ::cw::lib::String arg_newskindir);
    void set_curtain(bool arg_curtain_all);
    void clear_curtain();
    void store_filepath(::cw::lib::String arg_path, ::cw::lib::Real arg_mtime);
    bool is_modifiedfile();
    std::tuple<std::optional<std::shared_ptr<::cw::lib::Surface>>, bool, bool, std::optional<std::tuple<::cw::lib::String, ::cw::lib::Real>>> load_surface(::cw::lib::String arg_path, bool arg_mask, ::cw::lib::String arg_smoothing, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size, ::cw::lib::String arg_flag, std::shared_ptr<::cw::effectbooster::AnimationCounter> arg_doanime, bool arg_visible, bool arg_nocheckvisible, bool arg_can_loaded_scaledimage, std::optional<std::tuple<::cw::lib::String, ::cw::lib::Integer>> arg_animated_cellkey);
    void clear_background();
    bool load(std::vector<std::shared_ptr<::cw::data::CWPyElement>> arg_elements, ::cw::lib::Value arg_doanime, std::tuple<::cw::lib::String, ::cw::lib::Value> arg_ttype, bool arg_bginhrt, bool arg_nocheckvisible, bool arg_redraw);
    std::tuple<std::tuple<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>, std::tuple<::cw::lib::Integer, ::cw::lib::Integer>, ::cw::lib::String, bool, ::cw::lib::Integer, ::cw::lib::String>, bool> _create_bgbasedata(std::shared_ptr<::cw::data::CWPyElement> arg_e);
    std::optional<::cw::lib::Value> _create_imagecelldata(std::shared_ptr<::cw::data::CWPyElement> arg_e, bool arg_ignoreeffectbooster);
    bool remove_battle_background(::cw::lib::Value arg_doanime, std::tuple<::cw::lib::String, ::cw::lib::Value> arg_ttype, bool arg_ignoreeffectbooster);
    static std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> _getcolor(std::shared_ptr<::cw::data::CWPyElement> arg_e, ::cw::lib::String arg_xpath, ::cw::lib::Integer arg_r, ::cw::lib::Integer arg_g, ::cw::lib::Integer arg_b, ::cw::lib::Integer arg_a);
    ::cw::lib::Value _create_textcelldata(std::shared_ptr<::cw::data::CWPyElement> arg_e, bool arg_ignoreeffectbooster);
    ::cw::lib::Value _create_colorcelldata(std::shared_ptr<::cw::data::CWPyElement> arg_e, bool arg_ignoreeffectbooster);
    ::cw::lib::Value _create_pccelldata(std::shared_ptr<::cw::data::CWPyElement> arg_e, bool arg_ignoreeffectbooster);
    bool reload(bool arg_doanime, std::tuple<::cw::lib::String, ::cw::lib::Value> arg_ttype, bool arg_redraw, std::vector<::cw::lib::String> arg_cellname_list, std::vector<std::optional<std::vector<std::shared_ptr<::cw::data::CWPyElement>>>> arg_repldata_list, std::vector<std::optional<std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer>>> arg_movedata_list, bool arg_ignoreeffectbooster, bool arg_nocheckvisible);
    ::cw::lib::Value _reload(::cw::lib::Value arg_doanime, std::tuple<::cw::lib::String, ::cw::lib::Value> arg_ttype, bool arg_redraw, bool arg_force, bool arg_nocheckvisible, bool arg_redisplay, bool arg_beforeload, std::vector<::cw::lib::String> arg_cellname_list, std::vector<std::optional<std::vector<std::shared_ptr<::cw::data::CWPyElement>>>> arg_repldata_list, std::vector<std::optional<std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer>>> arg_movedata_list, bool arg_ignoreeffectbooster);
    bool _is_flagchanged(::cw::lib::Integer arg_bgtype, std::optional<::cw::lib::Value> arg_d);
    ::cw::lib::String _get_cellname(::cw::lib::Integer arg_bgtype, std::optional<::cw::lib::Value> arg_d);
    std::vector<::cw::lib::String> get_cellnames(bool arg_unique);
    ::cw::lib::Value _move_bgdata(::cw::lib::Integer arg_bgtype, ::cw::lib::Value arg_d, std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer> arg_movedata);
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> _calc_possize(::cw::lib::String arg_ctype, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_vals, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_movevals, bool arg_miniszero);
    std::tuple<bool, bool, bool> _add_imagecell(std::vector<std::tuple<::cw::lib::Integer, ::cw::lib::Value, ::cw::lib::String, ::cw::lib::Integer>>& arg_blitlist, std::vector<std::tuple<::cw::lib::Integer, std::optional<::cw::lib::Value>>>& arg_bgs, std::vector<std::tuple<::cw::lib::Integer, std::optional<::cw::lib::Value>>>& arg_oldbgs, ::cw::lib::Value arg_d, std::shared_ptr<::cw::effectbooster::AnimationCounter> arg_doanime, bool arg_nocheckvisible, bool arg_is_reload);
    bool _add_textcell(std::vector<std::tuple<::cw::lib::Integer, ::cw::lib::Value, ::cw::lib::String, ::cw::lib::Integer>>& arg_blitlist, std::vector<std::tuple<::cw::lib::Integer, std::optional<::cw::lib::Value>>>& arg_bgs, std::vector<std::tuple<::cw::lib::Integer, std::optional<::cw::lib::Value>>>& arg_oldbgs, ::cw::lib::Value arg_d, bool arg_nocheckvisible, bool arg_is_reload);
    bool _add_colorcell(std::vector<std::tuple<::cw::lib::Integer, ::cw::lib::Value, ::cw::lib::String, ::cw::lib::Integer>>& arg_blitlist, std::vector<std::tuple<::cw::lib::Integer, std::optional<::cw::lib::Value>>>& arg_bgs, std::vector<std::tuple<::cw::lib::Integer, std::optional<::cw::lib::Value>>>& arg_oldbgs, ::cw::lib::Value arg_d, bool arg_nocheckvisible, bool arg_is_reload);
    bool _add_pccell(std::vector<std::tuple<::cw::lib::Integer, ::cw::lib::Value, ::cw::lib::String, ::cw::lib::Integer>>& arg_blitlist, std::vector<std::tuple<::cw::lib::Integer, std::optional<::cw::lib::Value>>>& arg_bgs, std::vector<std::tuple<::cw::lib::Integer, std::optional<::cw::lib::Value>>>& arg_oldbgs, ::cw::lib::Value arg_d, bool arg_nocheckvisible, bool arg_is_reload);
    std::tuple<std::vector<std::tuple<::cw::lib::String, std::shared_ptr<::cw::image::ImageInfo>>>, bool> put_pccache(::cw::lib::Integer arg_pi);
    std::tuple<std::vector<std::tuple<::cw::lib::Integer, ::cw::lib::Value, ::cw::lib::String, ::cw::lib::Integer>>, bool> _load_after(bool arg_bginhrt, std::vector<std::tuple<::cw::lib::Integer, ::cw::lib::Value, ::cw::lib::String, ::cw::lib::Integer>> arg_blitlist, bool arg_doanime, bool arg_animated, std::optional<std::tuple<::cw::lib::String, ::cw::lib::Value>> arg_ttype, std::optional<std::shared_ptr<::cw::sprite::transition::Transition>> arg_transitspr, std::vector<std::tuple<::cw::lib::Integer, std::optional<::cw::lib::Value>>> arg_oldbgs, const std::vector<std::tuple<::cw::lib::Integer, std::optional<::cw::lib::Value>>>& arg_newbgs, bool arg_redraw, bool arg_redisplay);
};

class BgCell : public ::cw::sprite::base::CWPySprite {
public:
    BgCell() = default;
    virtual ~BgCell() = default;
    ::cw::lib::Value m_bgtype{};
    ::cw::lib::Value m_d{};
    ::cw::lib::Value m_flag{};
    ::cw::lib::Value m_rect{};
    ::cw::lib::Value m_rect_noscale{};
    // カーテンも含め、CWPySprite::m_tlayer をレイヤーの唯一の取得元にする。
    void initialize(::cw::lib::Integer arg_bgtype, ::cw::lib::Value arg_d, ::cw::lib::String arg_flag, ::cw::lib::Integer arg_layer, ::cw::lib::Integer arg_index);
};

class Jpy1TemporalSprite : public ::cw::sprite::base::CWPySprite {
public:
    Jpy1TemporalSprite() = default;
    virtual ~Jpy1TemporalSprite() = default;
    ::cw::lib::Value m_image{};
    ::cw::lib::Value m_rect{};
    void initialize(std::shared_ptr<::cw::lib::Surface> arg_background,
                    std::shared_ptr<Jpy1TemporalSprite> arg_owner = {});
};

class LifeBar : public ::cw::sprite::base::CWPySprite {
public:
    LifeBar() = default;
    virtual ~LifeBar() = default;
    ::cw::lib::Value m__image{};
    ::cw::lib::Value m__rect{};
    ::cw::lib::Value m_ccard{};
    ::cw::lib::Value m_group{};
    ::cw::lib::Value m_image{};
    ::cw::lib::Value m_rect{};
    std::shared_ptr<::cw::sprite::card::CWPyCard> m_ccard_typed{};
    std::shared_ptr<::cw::lib::Surface> m_base_image{};
    ::cw::lib::Rect m_base_rect{};
    void initialize(::cw::lib::Value arg_ccard);
    void update_scale();
    void click();
    void declick();
};

class NumberOfCards : public ::cw::sprite::base::CWPySprite {
public:
    NumberOfCards() = default;
    virtual ~NumberOfCards() = default;
    ::cw::lib::Value m_cardtype{};
    ::cw::lib::Value m_image{};
    ::cw::lib::Value m_pcard{};
    ::cw::lib::Value m_rect{};
    std::shared_ptr<::cw::sprite::card::PlayerCard> m_pcard_typed{};
    void initialize(std::shared_ptr<::cw::sprite::card::PlayerCard> arg_pcard, ::cw::lib::Integer arg_cardtype, std::shared_ptr<::cw::lib::Object> arg_spritegrp);
    void update_scale();
};

class PriceOfCard : public ::cw::sprite::base::CWPySprite {
public:
    PriceOfCard() = default;
    virtual ~PriceOfCard() = default;
    ::cw::lib::Value m_header{};
    ::cw::lib::Value m_image{};
    ::cw::lib::Value m_mcard{};
    ::cw::lib::Value m_rect{};
    std::shared_ptr<::cw::sprite::card::CWPyCard> m_mcard_typed{};
    std::optional<std::shared_ptr<::cw::header::CardHeader>> m_header_typed{};
    std::tuple<::cw::lib::Value, ::cw::lib::Value, ::cw::lib::Value, ::cw::lib::Integer> m_tlayer{};
    void initialize(std::shared_ptr<::cw::sprite::card::CWPyCard> arg_mcard, std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_header, std::shared_ptr<::cw::lib::Object> arg_spritegrp);
    void set_header(std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_header);
    void update_scale();
};

class TargetArrow : public ::cw::sprite::base::CWPySprite {
public:
    TargetArrow() = default;
    virtual ~TargetArrow() = default;
    ::cw::lib::Value m_image{};
    ::cw::lib::Value m_rect{};
    ::cw::lib::Value m_target{};
    std::shared_ptr<::cw::sprite::card::CWPyCard> m_target_typed{};
    void initialize(std::shared_ptr<::cw::sprite::card::CWPyCard> arg_target);
    void update_scale();
};

class ClickableSprite : public ::cw::sprite::base::SelectableSprite {
public:
    ClickableSprite() = default;
    virtual ~ClickableSprite() = default;
    ::cw::lib::Value m__clickedimage{};
    ::cw::lib::Value m__clickedrect{};
    bool m__clicking{};
    ::cw::lib::Value m__getimage{};
    ::cw::lib::Value m__getselimage{};
    // Python callableはValueへ潰さず、クリック時に同じ引数順で呼び出す。
    ::cw::lib::Callback m_getimage_cb{};
    ::cw::lib::Callback m_getselimage_cb{};
    std::optional<::cw::lib::Callback> m_lclickevent_cb{};
    std::optional<::cw::lib::Callback> m_rclickevent_cb{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> m_pos_noscale_typed{};
    ::cw::lib::Value m__image{};
    ::cw::lib::Value m__lclickevent{};
    ::cw::lib::Value m__pos_noscale{};
    ::cw::lib::Value m__rclickevent{};
    ::cw::lib::Value m__rect{};
    ::cw::lib::Value m__selclickedimage{};
    ::cw::lib::Value m__selimage{};
    ::cw::lib::Integer m_clickable_group{};
    ::cw::lib::Integer m_frame{};
    ::cw::lib::Value m_image{};
    ::cw::lib::String m_old_status{};
    ::cw::lib::Value m_rect{};
    ::cw::lib::Value m_spritegrp{};
    ::cw::lib::String m_status{};
    void initialize(::cw::lib::Callback arg_getimage, ::cw::lib::Callback arg_getselimage, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos_noscale, std::shared_ptr<::cw::lib::Object> arg_spritegrp, std::optional<::cw::lib::Callback> arg_lclickevent, std::optional<::cw::lib::Callback> arg_rclickevent);
    void update_scale();
    std::shared_ptr<::cw::lib::Surface> get_unselectedimage();
    std::shared_ptr<::cw::lib::Surface> get_selectedimage();
    void lclick_event();
    void rclick_event();
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
    void update_normal();
    void update_click();
    void update_hide();
    void update_delete();
};

class Curtain : public ::cw::sprite::base::SelectableSprite {
public:
    Curtain() = default;
    virtual ~Curtain() = default;
    ::cw::lib::Value m__is_selectable{};
    ::cw::lib::Value m_color{};
    ::cw::lib::Value m_cutter{};
    std::shared_ptr<::cw::lib::Surface> m_cutter_surface{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> m_cutter_pos{};
    ::cw::lib::Value m_image{};
    ::cw::lib::Value m_rect{};
    std::shared_ptr<::cw::sprite::base::CWPySprite> m_target{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> m_color_typed{0, 0, 128, 128};
    // Py の tlayer は一つ。登録と共通描画は基底のフィールドを共有する。
    void initialize(std::shared_ptr<::cw::sprite::base::CWPySprite> arg_target, std::shared_ptr<::cw::lib::Object> arg_spritegrp, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>> arg_color, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>> arg_layer, bool arg_cut_bgs, bool arg_is_selectable, bool arg_initialize);
    void update_scale();
    std::optional<std::shared_ptr<::cw::lib::Surface>> create_mask();
    void rclick_event();
    bool is_selection();
};

// SpriteGroup は背景が所有するスプライトの描画用の代理を保存す
// る。現在の所有者を解決し、共通のグループ更新でそれらのヒット判定も行う。
::cw::sprite::base::CWPySprite* background_layer_sprite_owner(
    const std::shared_ptr<::cw::lib::Sprite>& arg_sprite);
std::optional<::cw::lib::Integer> background_layer_sprite_layer(
    const std::shared_ptr<::cw::lib::Sprite>& arg_sprite);

class BattleCardImage : public ::cw::sprite::card::CWPyCard {
public:
    BattleCardImage() = default;
    virtual ~BattleCardImage() = default;
    ::cw::lib::Value m__image{};
    ::cw::lib::Value m__rect{};
    // Py の highspeed 属性は一つ。CWPyCard::m_highspeed
    // を直接使い、継承した表示アニメーション・非表示の更新が戦闘開始演出のフラグを参照できるようにする。
    ::cw::lib::Value m_image{};
    ::cw::lib::Value m_image_unzoomed{};
    ::cw::lib::Value m_rect{};
    void initialize();
    void update_battlestart();
    std::shared_ptr<::cw::lib::Object> update_image(bool arg_update_statusimg, std::optional<bool> arg_is_runningevent);
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
    void update_selection();
};

class InuseCardImage : public ::cw::sprite::card::CWPyCard {
public:
    InuseCardImage() = default;
    virtual ~InuseCardImage() = default;
    // Py は継承したカード画像・矩形・状態の属性を使う。アニ
    // メーション、位置決め、描画で一つの型付き状態を共有する。
    ::cw::lib::Value m_alpha{};
    ::cw::lib::Value m_center{};
    ::cw::lib::Value m_group{};
    ::cw::lib::Value m_header{};
    ::cw::lib::Value m_user{};
    std::shared_ptr<::cw::sprite::card::CWPyCard> m_user_typed{};
    std::shared_ptr<::cw::header::CardHeader> m_header_typed{};
    bool m_center_typed{};
    std::optional<::cw::lib::Integer> m_alpha_typed{};
    void initialize(std::shared_ptr<::cw::sprite::card::CWPyCard> arg_user, std::shared_ptr<::cw::header::CardHeader> arg_header, ::cw::lib::String arg_status, bool arg_center, std::optional<::cw::lib::Integer> arg_alpha, bool arg_fore);
    void update_scale();
    std::shared_ptr<::cw::lib::Object> update_image(bool arg_update_statusimg, std::optional<bool> arg_is_runningevent);
    void update_selection();
};

inline constexpr ::cw::lib::Integer g_BG_SEPARATOR=-1;
inline constexpr ::cw::lib::Integer g_BG_IMAGE=0;
inline constexpr ::cw::lib::Integer g_BG_TEXT=1;
inline constexpr ::cw::lib::Integer g_BG_COLOR=2;
inline constexpr ::cw::lib::Integer g_BG_PC=3;
extern ::cw::lib::Value g_CellData;
extern ::cw::lib::Value g_ColorCellData;
extern ::cw::lib::Value g_ImageCellData;
extern ::cw::lib::Value g_PCCellData;
extern ::cw::lib::Value g_TextCellData;
extern ::cw::lib::Value g__BlitData;

void BackGround_update_scale_func();
::cw::lib::Value BackGround__reload_lambda_line_1021(::cw::lib::Value arg_item);
void BackGround__reload_clear_forgrounds();
::cw::lib::Integer _add_splayerval(::cw::lib::Integer arg_layer);
bool _equals_bgs(std::vector<std::tuple<::cw::lib::Integer, std::optional<::cw::lib::Value>>> arg_bgs1, std::vector<std::tuple<::cw::lib::Integer, std::optional<::cw::lib::Value>>> arg_bgs2, bool arg_visibleonly);
::cw::lib::Value _equals_bgs_lambda_line_1618(::cw::lib::Value arg_t);
::cw::lib::Value _equals_bgs_lambda_line_1619(::cw::lib::Value arg_t);
bool _equals_bgs_is_visible(std::tuple<::cw::lib::Integer, std::optional<::cw::lib::Value>> arg_t);
std::shared_ptr<::cw::lib::Object> _draw_bgcell(std::shared_ptr<::cw::lib::Surface> arg_surface, std::tuple<::cw::lib::Integer, ::cw::lib::Value> arg_bgdata, std::optional<std::shared_ptr<::cw::lib::Object>> arg_allclip);
std::vector<std::shared_ptr<::cw::lib::Object>> layered_draw_ex(std::shared_ptr<::cw::lib::Object> arg_layered_updates, std::shared_ptr<::cw::lib::Surface> arg_surface);
std::vector<std::shared_ptr<::cw::lib::Sprite>> sprites_from_layer(std::shared_ptr<::cw::lib::Object> arg_group, ::cw::lib::Integer arg_layer);
void main();
}

