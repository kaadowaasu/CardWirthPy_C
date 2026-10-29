#pragma once

// cw/sprite/card.py の移植。CWPyCard が Python の
// 状態遷移全体を保持し、最終的なテクスチャー転送・合成は SDL 描画器が担当する。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include "cw/character.hpp"
#include "cw/sprite/base.hpp"

namespace cw::sprite::card {
std::optional<::cw::lib::Integer> card_layer_sprite_layer(
    const std::shared_ptr<::cw::lib::Sprite>& arg_sprite);
class CWPyCard : public ::cw::sprite::base::SelectableSprite {
public:
    CWPyCard() = default;
    virtual ~CWPyCard();
    // Python版ではこれらはカードごとの状態であり、クラス共有値ではない。
    std::optional<std::shared_ptr<::cw::image::CardImage>> m__cardimg{};
    std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> m__center_noscale{};
    bool m__clicking{};
    // animate_sprite() はクリックハンドラーの実行中にも入力を処理する
    // 。カードごとに入力・アニメーションのガードを持ち、同じボタン解放が PlayerCard/EnemyCard
    // に再入して戦闘の選択状態を壊さないようにする。
    bool m_input_dispatching{};
    bool m_click_animating{};
    std::shared_ptr<::cw::lib::Surface> m__image{};
    std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> m__pos_noscale{};
    ::cw::lib::Rect m__rect{};
    std::map<::cw::lib::Integer, bool> m_actions{};
    std::optional<::cw::lib::Integer> m_alpha{};
    // 表示の不透明度は一度だけ適用し、共有する元画像には書き込まない。
    std::shared_ptr<::cw::lib::Surface> get_render_image() const;
    ::cw::lib::String m_arg{};
    bool m_battlespeed{};
    bool m_cardtarget{};
    ::cw::lib::String m_command{};
    ::cw::lib::Integer m_dealspeed{-1};
    bool m_debug_only{};
    std::optional<::cw::lib::String> m_flag{};
    bool m_hide_inusecardimg{};
    bool m_highspeed{};
    std::optional<std::shared_ptr<::cw::sprite::background::InuseCardImage>> m_inusecardimg{};
    bool m_reversed{};
    ::cw::lib::Integer m_scale{};
    bool m_spchars{};
    std::vector<std::tuple<std::shared_ptr<::cw::lib::Surface>, ::cw::lib::Rect>> m_zoomimgs{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> m_zoomsize_noscale{};
    void initialize(::cw::lib::String arg_status, std::optional<::cw::lib::String> arg_flag);
    std::shared_ptr<::cw::image::CardImage> get_cardimg();
    bool has_cardimg();
    void release_animated_images();
    virtual ::cw::lib::String get_showingname();
    bool is_flagtrue();
    static bool is_flagtrue_static(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    std::shared_ptr<::cw::lib::Surface> get_unselectedimage() override;
    std::shared_ptr<::cw::lib::Surface> get_selectedimage() override;
    bool is_selection() override;
    void set_alpha(std::optional<::cw::lib::Integer> arg_alpha);
    std::shared_ptr<::cw::lib::Surface> get_animeimage();
    ::cw::lib::Rect get_animerect();
    ::cw::lib::Rect get_baserect();
    std::vector<::cw::lib::Integer> _get_dealingscales();
    ::cw::lib::Integer get_dealspeed(bool arg_battlespeed);
    ::cw::lib::Integer _get_dealspeed();
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs) override;
    void update_normal();
    virtual void update_delete();
    void update_reversed();
    void update_hidden();
    void update_reverse();
    void reverse();
    void _reverse();
    void update_click();
    void update_deal();
    void deal();
    void update_hide();
    void hide();
    void update_lateralvibe();
    void update_axialvibe();
    void update_zoomin();
    void update_zoomin_slow();
    void update_zoomout();
    void update_zoomout_slow();
    void _update_zoominout(::cw::lib::Integer arg_ds, bool arg_inout);
    void clear_zoomimgs();
    void update_shiftup();
    void update_shiftdown();
    void update_scale() override;
    std::optional<::cw::lib::Rect> update_image(bool arg_update_statusimg, std::optional<bool> arg_is_runningevent);
    void clear_image(bool arg_move);
    virtual void set_pos_noscale(std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_pos_noscale, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_center_noscale);
    virtual void set_pos(std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_pos, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_center);
    virtual std::tuple<::cw::lib::Integer, ::cw::lib::Integer> get_pos_noscale();
    virtual void set_scale(::cw::lib::Integer arg_scale);
    virtual void set_size(::cw::lib::Integer arg_width, ::cw::lib::Integer arg_height);
    void set_cardtarget();
    void clear_cardtarget();
};

// 同じカードの代理を追加・並べ替えする。明示レイヤーは Python の add_layer
// に対応し、通常更新では定義済みのカード・対象の既定レイヤーを使う。
void refresh_card_layer(CWPyCard& arg_card,
    std::optional<::cw::lib::Integer> arg_layer = std::nullopt);
// Python の cardgrp.has(card) 相当。所属は PlayerCard
// 参照の生存だけでなく、現在のグループに代理が実在するかで判定する。
bool is_in_cardgrp(const CWPyCard& arg_card);
// Python の cardgrp.remove(mcard) 相当。同一性に基づいて、このカードのグループ内の代理を除く。
void remove_from_cardgrp(CWPyCard& arg_card);
// エリア置換時は、CWPy::m_mcards から消えた後もアニメ
// ーション・選択の別名参照に残る MenuCard の代理も除く。
void remove_all_menu_cards_from_cardgrp();
// Python の clear_inusecardimg()
// は選択解除時に一時的な前面のライフバーを除く。ネ
// イティブ代理の寿命も同期し、通常合成では描かれな
// い古いライフバーが遷移画像に入らないようにする。
void clear_lifebars(std::optional<CWPyCard*> arg_user = std::nullopt);
// 選択キャラクターの前面ライフバー。クリック時のアニメーションと寿命を共有する。
void show_lifebar(CWPyCard& arg_card);
std::shared_ptr<::cw::sprite::background::LifeBar> find_owned_lifebar(
    const ::cw::sprite::background::LifeBar* arg_bar);
CWPyCard* card_layer_sprite_owner(const std::shared_ptr<::cw::lib::Sprite>& arg_sprite);

class EnemyCard : public ::cw::sprite::card::CWPyCard, public ::cw::character::Enemy {
public:
    EnemyCard() = default;
    virtual ~EnemyCard() = default;
    bool m__init{};
    std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> m__init_pos_noscale{};
    std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>> m__moveddata{};
    ::cw::lib::String m__name{};
    ::cw::lib::String m_cardgroup{};
    std::shared_ptr<::cw::event::EventEngine> m_events{};
    ::cw::lib::String m_fpath{};
    ::cw::lib::Integer m_height{};
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> m_imgpaths{};
    ::cw::lib::Integer m_index{};
    std::shared_ptr<::cw::data::CWPyElement> m_mcarddata{};
    // 遅延初期化も含め、倍率と特殊文字の方針は CWPyCard
    // が所有する。Python の属性も一つだけ。
    ::cw::lib::Integer m_width{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> m_zoomsize_noscale{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_mcarddata, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos_noscale, ::cw::lib::String arg_status, bool arg_addgroup, ::cw::lib::Integer arg_index, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>> arg_moveddata);
    bool initialize_2();
    bool is_initialized() override;
    void update_skin();
    std::shared_ptr<::cw::image::CharacterCardImage> get_cardimg();
    ::cw::lib::String get_showingname() override;
    void update_name();
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs) override;
    void update_delete() override;
    void lclick_event() override;
    void rclick_event() override;
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> get_pos_noscale() override;
    void set_pos_noscale(std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_pos_noscale, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_center_noscale) override;
    void set_scale(::cw::lib::Integer arg_scale) override;
};

class FriendCard : public ::cw::sprite::card::CWPyCard, public ::cw::character::Friend {
public:
    FriendCard() = default;
    virtual ~FriendCard() = default;
    // すべてのキャラクター種別の現在の XML は Character::m_data_typed が所有する。
    ::cw::lib::String m_fpath{};
    ::cw::lib::Integer m_id{};
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> m_imgpaths{};
    ::cw::lib::Integer m_index{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> m_tlayer_t{};
    // アニメーション処理の拡大増分は CWPyCard が所有する。
    void initialize(::cw::lib::Value arg_data, ::cw::lib::Integer arg_index);
    std::shared_ptr<::cw::image::CharacterCardImage> get_cardimg();
    ::cw::lib::String get_showingname();
    void update_name();
    void update_delete();
    void lclick_event();
    void rclick_event();
};

class MenuCard : public ::cw::sprite::card::CWPyCard {
public:
    MenuCard() = default;
    virtual ~MenuCard() = default;
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> m__data{};
    bool m__init{};
    bool m__is_backpack{};
    bool m__is_storehouse{};
    ::cw::lib::String m__name{};
    std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> m__pos_noscale2{};
    ::cw::lib::String m_author{};
    ::cw::lib::String m_cardgroup{};
    // コマンド、表示アニメーション速度、倍率、特殊文字の方針は継承する。
    ::cw::lib::String m_desc{};
    std::shared_ptr<::cw::event::EventEngine> m_events{};
    ::cw::lib::Integer m_height{};
    ::cw::lib::Integer m_index{};
    ::cw::lib::String m_name{};
    bool m_negaflag{};
    ::cw::lib::String m_scenario{};
    ::cw::lib::Integer m_width{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos_noscale, ::cw::lib::String arg_status, bool arg_addgroup, ::cw::lib::Integer arg_index, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>> arg_moveddata, std::optional<bool> arg_splayer);
    bool initialize_2();
    ::cw::lib::String get_showingname();
    void update_name();
    std::shared_ptr<::cw::image::CardImage> get_cardimg();
    bool is_initialized();
    bool is_backpack();
    bool is_storehouse();
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
    void lclick_event();
    void rclick_event();
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> get_pos_noscale();
    void set_pos_noscale(std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_pos_noscale, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_center_noscale);
    void set_scale(::cw::lib::Integer arg_scale);
    void set_size(::cw::lib::Integer arg_width, ::cw::lib::Integer arg_height);
};

class PlayerCard : public ::cw::sprite::card::CWPyCard, public ::cw::character::Player {
public:
    PlayerCard() = default;
    virtual ~PlayerCard() = default;
    // CWPyCard/CWPySprite の状態を再宣言しない。多重継承した Character::m_status/m_reversed
    // と衝突するため、実装では CWPyCard:: または Character::
    // を明示し、同じ Python 属性を共有する。保存と表示の両
    // 方に Character::m_data_typed を使う。
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> m_imgpaths{};
    ::cw::lib::Integer m_index_typed{};
    bool m_can_loaded_scaledimage{};
    void initialize(std::shared_ptr<::cw::data::CWPyElementTree> arg_data, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos_noscale, ::cw::lib::String arg_status, ::cw::lib::Integer arg_index);
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs) override;
    void set_pos(std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_pos, std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> arg_center);
    std::shared_ptr<::cw::image::CharacterCardImage> get_cardimg();
    ::cw::lib::String get_showingname();
    void set_name(::cw::lib::String arg_name);
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> set_images(std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_paths) override;
    void update_levelup();
    void update_delete();
    void update_vanish();
    void lclick_event();
    void rclick_event();
    void set_level(::cw::lib::Integer arg_value, bool arg_regulate, bool arg_debugedit, std::optional<std::shared_ptr<::cw::data::Party>> arg_backpack_party, bool arg_revert_cardpocket) override;
    bool adjust_level(bool arg_fromscenario);
    void lost();
};

::cw::lib::Integer CWPyCard__update_zoominout_calc_zoom(::cw::lib::Integer arg_zoom_val);
void PlayerCard_set_images_func();
void PlayerCard_set_images_func_func();
void _select_action(std::shared_ptr<::cw::sprite::card::CWPyCard> arg_sprite);
std::tuple<std::vector<std::shared_ptr<::cw::image::ImageInfo>>, std::vector<bool>> imageinfos_to_pathdata(std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_infos);
void main();
}

