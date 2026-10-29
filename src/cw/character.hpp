#pragma once

// cw/character.pyに
// 対応する、キャラクターの宣言。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include <mutex>

namespace cw::character {
class AlbumPage : public ::cw::lib::Object {
public:
    AlbumPage() = default;
    virtual ~AlbumPage() = default;
    ::cw::lib::Value m_data{};
    ::cw::lib::Value m_level{};
    ::cw::lib::String m_name{};
    void initialize(std::shared_ptr<::cw::data::CWPyElementTree> arg_data);
    ::cw::lib::String get_showingname();
    std::map<::cw::lib::String, ::cw::lib::Integer> get_specialcoupons();
};

class Character : public ::cw::lib::Object,
                  public std::enable_shared_from_this<Character> {
public:
    Character() = default;
    virtual ~Character() = default;
    // Valueは直接のリスト・辞書型を持たないため、型付きの状態も保持する。
    // Value側ではObjectPtrに格納したVariantSequenceなどを介して列を表す。
    // 公開Valueフィールドは既存の移植コードとの互換用に残す。
    struct DeckRegistrationItem {
        ::cw::lib::String name{};
        ::cw::lib::String desc{};
        ::cw::lib::String type{};
        ::cw::lib::String scenario{};
        ::cw::lib::String author{};
        ::cw::lib::String personal{};
    };
    struct DeckRegistration {
        ::cw::lib::String name{};
        ::cw::lib::String type{};
        ::cw::lib::String scenario{};
        ::cw::lib::String author{};
        std::vector<DeckRegistrationItem> items{};
    };
    inline static ::cw::lib::Integer s__BTYPE_ACTION{};
    inline static ::cw::lib::Integer s__BTYPE_BEAST{};
    inline static ::cw::lib::Integer s__BTYPE_CAST{};
    inline static ::cw::lib::Integer s__BTYPE_ITEM{};
    inline static ::cw::lib::Integer s__BTYPE_ITEM_USE{};
    inline static ::cw::lib::Integer s__BTYPE_SKILL{};
    inline static ::cw::lib::Integer s__BTYPE_STATUS{};
    inline static std::map<::cw::lib::Integer, bool> s_actions{};
    // Pythonのstatusはカードごとの表示状態であり、クラス共有値ではない。 ここでは単
    // 独のCharacter用の代替値だけを持つ。スプライトを持つキャラクターは CWPyCard
    // から動作状態を取得する（character.cppのcard_statusを参照）。
    ::cw::lib::String m_status{};
    ::cw::lib::String get_card_status() const;
    inline static std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>> s_versionhint{};
    ::cw::lib::Integer m__coeff_ep{};
    ::cw::lib::Real m__coeff_level{};
    bool m__vanished{};
    std::map<std::tuple<::cw::lib::String, ::cw::lib::String>, std::optional<::cw::lib::Integer>> m__voc_tbl{};
    bool m_actionautoselected{};
    std::tuple<::cw::lib::Value, ::cw::lib::Value, ::cw::lib::Value> m_actiondata{};
    bool m_actionend{};
    ::cw::lib::Integer m_actionorder{};
    ::cw::lib::Integer m_antimagic{};
    ::cw::lib::Integer m_bind{};
    ::cw::lib::Value m_cardpocket{};
    ::cw::lib::ValueMap m_coupons{};
    ::cw::lib::Value m_data{};
    std::shared_ptr<::cw::deck::Deck> m_deck{};
    std::map<::cw::lib::String, ::cw::lib::Integer> m_enhance{};
    ::cw::lib::Integer m_enhance_act{};
    ::cw::lib::Integer m_enhance_act_dur{};
    ::cw::lib::Integer m_enhance_avo{};
    ::cw::lib::Integer m_enhance_avo_dur{};
    ::cw::lib::Integer m_enhance_def{};
    ::cw::lib::Integer m_enhance_def_dur{};
    ::cw::lib::Integer m_enhance_res{};
    ::cw::lib::Integer m_enhance_res_dur{};
    ::cw::lib::Integer m_faceup{};
    std::map<::cw::lib::String, bool> m_feature{};
    std::vector<bool> m_hold_all{};
    bool m_is_entered_battle{};
    ::cw::lib::Integer m_level{};
    ::cw::lib::Integer m_life{};
    ::cw::lib::Integer m_maxlife{1};
    std::map<::cw::lib::String, ::cw::lib::Real> m_mental{};
    ::cw::lib::String m_mentality{};
    ::cw::lib::Integer m_mentality_dur{};
    ::cw::lib::String m_name{};
    std::map<::cw::lib::String, bool> m_noeffect{};
    ::cw::lib::Integer m_paralyze{};
    std::map<::cw::lib::String, ::cw::lib::Real> m_physical{};
    ::cw::lib::Integer m_poison{};
    std::map<::cw::lib::String, bool> m_resist{};
    bool m_reversed{};
    ::cw::lib::Integer m_silence{};
    std::optional<std::shared_ptr<::cw::header::CardHeader>> m_test_aptitude{};
    // 時間制限付きクーポンの所属は、m_timedcoupons_typedだけで管理する。
    ::cw::lib::Value m_versionhint{};
    std::map<::cw::lib::String, bool> m_weakness{};
    std::map<::cw::lib::String, ::cw::lib::Real> m_enhance_typed{};
    // PC・敵・同行者の正式な実行時XML。スプライトの派生クラスに別の所有ツ
    // リーを持たせない。 m_dataは従来のValue形式での参照用とする。
    std::shared_ptr<::cw::data::CWPyElementTree> m_data_typed{};
    std::optional<::cw::lib::Value> m_action_target_typed{};
    std::optional<std::shared_ptr<::cw::header::CardHeader>> m_action_header_typed{};
    std::vector<std::tuple<std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>>, std::shared_ptr<::cw::header::CardHeader>>> m_action_beasts_typed{};
    std::tuple<std::vector<std::shared_ptr<::cw::header::CardHeader>>,
               std::vector<std::shared_ptr<::cw::header::CardHeader>>,
               std::vector<std::shared_ptr<::cw::header::CardHeader>>> m_cardpocket_typed{};
    std::vector<DeckRegistration> m_deck_registrations_typed{};
    std::map<::cw::lib::String, std::tuple<::cw::lib::Integer, std::shared_ptr<::cw::data::CWPyElement>>> m_coupons_typed{};
    std::set<::cw::lib::String> m_timedcoupons_typed{};
    std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>> m_versionhint_typed{};
    void initialize(std::shared_ptr<::cw::data::CWPyElementTree> arg_data);
    ::cw::lib::String get_showingname();
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> get_imagepaths();
    virtual std::vector<std::shared_ptr<::cw::image::ImageInfo>> set_images(std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_paths);
    ::cw::lib::String get_name();
    virtual void set_name(::cw::lib::String arg_name);
    ::cw::lib::String get_description();
    void set_description(::cw::lib::String arg_desc);
    void set_maxlife(::cw::lib::Integer arg_value);
    void set_physical(::cw::lib::String arg_name, ::cw::lib::Real arg_value);
    void set_mental(::cw::lib::String arg_name, ::cw::lib::Real arg_value);
    void set_feature(::cw::lib::String arg_name, bool arg_value);
    void set_noeffect(::cw::lib::String arg_name, bool arg_value);
    void set_resist(::cw::lib::String arg_name, bool arg_value);
    void set_weakness(::cw::lib::String arg_name, bool arg_value);
    void set_enhance(::cw::lib::String arg_name, ::cw::lib::Integer arg_value);
    void add_deck_registration(::cw::lib::String arg_name, std::vector<std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, std::optional<::cw::lib::String>, std::optional<::cw::lib::String>>> arg_items, std::optional<::cw::lib::String> arg_rtype, std::optional<::cw::lib::String> arg_scenario, std::optional<::cw::lib::String> arg_author);
    void remove_deck_registration(::cw::lib::String arg_name, std::optional<::cw::lib::String> arg_rtype);
    bool rename_deck_registration(::cw::lib::String arg_name, ::cw::lib::String arg_new_name, std::optional<::cw::lib::String> arg_rtype);
    std::vector<std::map<::cw::lib::String, ::cw::lib::Value>> get_deck_registrations();
private:
    // 生成時のXML読み込み用。実行中はカードヘッダーを再生成せず、
    // Pythonのself.cardpocketに対応するm_cardpocket_typed
    // を使う。 通常の持ち物参照がXMLの再構成へ
    // 置き換わらないよう、この関数は非公開にする。
    std::tuple<std::vector<std::shared_ptr<::cw::header::CardHeader>>, std::vector<std::shared_ptr<::cw::header::CardHeader>>, std::vector<std::shared_ptr<::cw::header::CardHeader>>> load_cardpocket_from_xml();
public:
    void replace_cardposition(::cw::lib::Integer arg_cardtype, std::shared_ptr<::cw::header::CardHeader> arg_header1, std::shared_ptr<::cw::header::CardHeader> arg_header2);
    std::set<::cw::lib::String> get_keycodes(bool arg_skill, bool arg_item, bool arg_beast);
    std::optional<std::shared_ptr<::cw::header::CardHeader>> find_keycode(::cw::lib::String arg_keycode, bool arg_skill, bool arg_item, bool arg_beast, bool arg_hand, ::cw::lib::String arg_condition);
    virtual void lost();
    bool is_normal();
    bool is_panic();
    bool is_brave();
    bool is_overheat();
    bool is_confuse();
    bool is_sleep();
    bool is_paralyze();
    bool is_poison();
    bool is_bind();
    bool is_silence();
    bool is_faceup();
    bool is_antimagic();
    static bool calc_petrified(::cw::lib::Integer arg_paralyze);
    bool is_petrified();
    bool is_unconscious();
    static bool calc_heavyinjured(::cw::lib::Integer arg_life, ::cw::lib::Integer arg_maxlife);
    bool is_heavyinjured();
    static bool calc_injured(::cw::lib::Integer arg_life, ::cw::lib::Integer arg_maxlife);
    bool is_injured();
    bool is_injuredall();
    virtual bool is_inactive(bool arg_check_reversed = true);
    bool is_active();
    virtual bool is_dead();
    bool is_alive();
    bool is_fine();
    bool is_analyzable();
    bool is_avoidable(bool arg_use_enhance = true);
    bool is_resistable(bool arg_use_enhance = true);
    bool is_reversed();
    bool is_vanished();
    ::cw::lib::Integer has_beast();
    bool is_enhanced_act();
    bool is_enhanced_res();
    bool is_enhanced_avo();
    bool is_enhanced_def();
    bool is_upaction();
    bool is_upresist();
    bool is_upavoid();
    bool is_updefense();
    bool is_downaction();
    bool is_downresist();
    bool is_downavoid();
    bool is_downdefense();
    bool is_effective(std::shared_ptr<::cw::header::CardHeader> arg_header, std::shared_ptr<::cw::data::CWPyElement> arg_motion);
    bool use_card(::cw::lib::Value arg_targets, std::shared_ptr<::cw::header::CardHeader> arg_header);
    void throwaway_card(std::shared_ptr<::cw::header::CardHeader> arg_header, bool arg_from_event, bool arg_update_image);
    void action();
    void set_action(std::optional<::cw::lib::Value> arg_target, std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_header, std::optional<std::vector<std::tuple<std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>>, std::shared_ptr<::cw::header::CardHeader>>>> arg_beasts, bool arg_py_auto);
    void replace_action_header(const std::shared_ptr<::cw::header::CardHeader>& arg_old,
                               const std::shared_ptr<::cw::header::CardHeader>& arg_replacement);
    void _add_priorityacts(::cw::lib::Value arg_target, std::shared_ptr<::cw::header::CardHeader> arg_h);
    void adjust_action();
    void clear_action();
    bool is_autoselectedpenalty(std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_header);
    bool decide_outcome(::cw::lib::Integer arg_level, std::tuple<::cw::lib::String, ::cw::lib::String> arg_vocation, ::cw::lib::Integer arg_thresholdbonus, ::cw::lib::Integer arg_enhance, ::cw::lib::Integer arg_subbonus);
    bool decide_misfire(::cw::lib::Integer arg_level);
    ::cw::lib::Integer decide_actionorder();
    void decide_action();
    std::tuple<std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>>, std::optional<std::shared_ptr<::cw::header::CardHeader>>> decide_usecard(std::vector<std::tuple<std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>>, std::shared_ptr<::cw::header::CardHeader>>> arg_headers);
    std::shared_ptr<::cw::data::CWPyElement> _get_motions(std::shared_ptr<::cw::header::CardHeader> arg_header);
    bool _is_bonusedmtype(::cw::lib::String arg_mtype);
    std::tuple<::cw::lib::Integer, std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>>> _get_targetingbonus_and_targets(std::shared_ptr<::cw::header::CardHeader> arg_header, std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>> arg_targets, bool arg_beast);
    ::cw::lib::Integer get_targetingbonus(::cw::lib::String arg_mtype, bool arg_beast);
    std::vector<std::shared_ptr<::cw::header::CardHeader>> get_pocketcards(::cw::lib::Integer arg_index);
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> get_cardpocketspace();
    bool has_expanded_pc_card_limits() const;
    ::cw::lib::Real skill_distribution_rate();
    static ::cw::lib::Integer calc_lifeper(::cw::lib::Integer arg_life, ::cw::lib::Integer arg_maxlife);
    ::cw::lib::Integer get_lifeper();
    ::cw::lib::Integer get_bonus(std::tuple<::cw::lib::String, ::cw::lib::String> arg_vocation, bool arg_enhance_act = true);
    ::cw::lib::Integer get_vocation_val(std::tuple<::cw::lib::String, ::cw::lib::String> arg_vocation);
    void _clear_vocationcache();
    ::cw::lib::Integer get_enhance_act();
    ::cw::lib::Real get_enhance_def();
    ::cw::lib::Integer get_enhance_res();
    ::cw::lib::Integer get_enhance_avo();
    ::cw::lib::Real _get_enhance_impl_f(::cw::lib::String arg_name, ::cw::lib::Integer arg_initvalue, ::cw::lib::Integer arg_enhindex, std::optional<::cw::lib::Integer> arg_btype);
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> _get_enhance_impl_i(::cw::lib::String arg_name, ::cw::lib::Integer arg_initvalue, ::cw::lib::Integer arg_enhindex, std::optional<::cw::lib::Integer> arg_btype);
    std::tuple<::cw::lib::Real, ::cw::lib::Real, ::cw::lib::Integer> _get_enhance_impl(::cw::lib::String arg_name, ::cw::lib::Integer arg_initvalue, ::cw::lib::Integer arg_enhindex, std::optional<::cw::lib::Integer> arg_btype);
    std::set<::cw::lib::String> get_coupons();
    std::set<::cw::lib::String> _get_coupons();
    std::optional<::cw::lib::Integer> get_couponvalue(::cw::lib::String arg_name, bool arg_raiseerror);
    std::optional<::cw::lib::Integer> _get_couponvalue(::cw::lib::String arg_name, bool arg_raiseerror);
    bool has_coupon(::cw::lib::String arg_coupon);
    bool _has_coupon(::cw::lib::String arg_coupon);
    bool has_coupon_nolock(::cw::lib::String arg_coupon);
    ::cw::lib::Integer get_couponsvalue();
    ::cw::lib::Integer _get_couponsvalue();
    std::map<::cw::lib::String, ::cw::lib::Integer> get_specialcoupons();
    std::map<::cw::lib::String, ::cw::lib::Integer> _get_specialcoupons();
    bool replace_allcoupons(std::vector<std::tuple<::cw::lib::String, ::cw::lib::Integer>> arg_seq, std::optional<::cw::lib::Value> arg_syscoupons);
    ::cw::lib::String get_sex();
    ::cw::lib::String _get_sex();
    void set_sex(::cw::lib::String arg_sex);
    void _set_sex(::cw::lib::String arg_sex);
    bool has_sex();
    bool _has_sex();
    ::cw::lib::String get_age();
    ::cw::lib::String _get_age();
    void set_age(::cw::lib::String arg_age);
    void _set_age(::cw::lib::String arg_age);
    bool has_age();
    bool _has_age();
    ::cw::lib::String get_talent();
    ::cw::lib::String _get_talent();
    void set_talent(::cw::lib::String arg_talent);
    void _set_talent(::cw::lib::String arg_talent);
    bool has_talent();
    bool _has_talent();
    std::set<::cw::lib::String> get_makings();
    std::set<::cw::lib::String> _get_makings();
    void set_makings(std::vector<::cw::lib::String> arg_makings);
    void _set_makings(std::vector<::cw::lib::String> arg_makings);
    void set_race(std::shared_ptr<::cw::header::RaceHeader> arg_race);
    void _set_race(std::shared_ptr<::cw::header::RaceHeader> arg_race);
    std::shared_ptr<::cw::header::RaceHeader> get_race();
    std::shared_ptr<::cw::header::RaceHeader> _get_race();
    ::cw::lib::Real get_levelcoeff();
    ::cw::lib::Real _get_levelcoeff();
    ::cw::lib::Integer _get_epcoeff();
    void count_timedcoupon(::cw::lib::Integer arg_value);
    void set_coupon(::cw::lib::String arg_name, ::cw::lib::Integer arg_value);
    void _set_coupon(::cw::lib::String arg_name, ::cw::lib::Integer arg_value, bool arg_update);
    std::set<::cw::lib::String> get_timedcoupons();
    bool remove_coupon(::cw::lib::String arg_name);
    bool _remove_coupon(::cw::lib::String arg_name, bool arg_update);
    void reverse();
    void remove_timedcoupons(bool arg_battleonly);
    void remove_numbercoupon();
    ::cw::lib::Integer find_coupon(::cw::lib::Callback arg_matcher, ::cw::lib::Integer arg_startindex);
    std::tuple<::cw::lib::String, ::cw::lib::Integer> get_coupon_at(::cw::lib::Integer arg_index);
    ::cw::lib::Integer coupons_len();
    ::cw::lib::Integer get_limitlevel();
    ::cw::lib::Integer _get_limitlevel();
    ::cw::lib::Integer check_level();
    ::cw::lib::Integer get_levelmax();
    ::cw::lib::Integer _get_levelmax(std::map<::cw::lib::String, ::cw::lib::Integer> arg_coupons);
    virtual void set_level(::cw::lib::Integer arg_value, bool arg_regulate, bool arg_debugedit, std::optional<std::shared_ptr<::cw::data::Party>> arg_backpack_party, bool arg_revert_cardpocket);
    void add_cardpocketmemory(std::shared_ptr<::cw::header::CardHeader> arg_header, bool arg_personal);
    void revert_cardpocket(std::optional<std::shared_ptr<::cw::data::Party>> arg_backpack_party);
    void _revert_cardpocket(std::optional<std::shared_ptr<::cw::data::Party>> arg_backpack_party);
    void revert_personalpocket(std::vector<std::shared_ptr<::cw::header::CardHeader>>& arg_headers, bool arg_force);
    void _revert_personalpocket(std::vector<std::shared_ptr<::cw::header::CardHeader>>& arg_headers, bool arg_force);
    void set_unconsciousstatus();
    void adjust_beast();
    void set_fullrecovery(bool arg_decideaction = false);
    ::cw::lib::Integer set_life(::cw::lib::Integer arg_value);
    ::cw::lib::Integer set_paralyze(::cw::lib::Integer arg_value);
    ::cw::lib::Integer set_poison(::cw::lib::Integer arg_value);
    void set_mentality(::cw::lib::String arg_name, ::cw::lib::Integer arg_value, bool arg_overwrite = true);
    void set_bind(::cw::lib::Integer arg_value, bool arg_overwrite = true);
    void set_silence(::cw::lib::Integer arg_value, bool arg_overwrite = true);
    void set_faceup(::cw::lib::Integer arg_value, bool arg_overwrite = true);
    void set_antimagic(::cw::lib::Integer arg_value, bool arg_overwrite = true);
    void set_vanish(bool arg_battlespeed = false);
    void cancel_vanish();
    void commit_vanish();
    void set_enhance_act(::cw::lib::Integer arg_value, ::cw::lib::Integer arg_duration);
    void set_enhance_avo(::cw::lib::Integer arg_value, ::cw::lib::Integer arg_duration);
    void set_enhance_res(::cw::lib::Integer arg_value, ::cw::lib::Integer arg_duration);
    void set_enhance_def(::cw::lib::Integer arg_value, ::cw::lib::Integer arg_duration);
    void set_skillpower(::cw::lib::Integer arg_value = 999);
    bool set_beast(std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_element, bool arg_vanish, bool arg_is_scenariocard);
    bool can_addbeast(std::shared_ptr<::cw::data::CWPyElement> arg_carddata);
    void decrease_physical(::cw::lib::String arg_stype, ::cw::lib::Integer arg_time);
    void set_timeelapse(::cw::lib::Integer arg_time, bool arg_fromevent);
    void set_hold_all(::cw::lib::Integer arg_pocket, bool arg_value);
};

class Enemy : public ::cw::character::Character {
public:
    Enemy() = default;
    virtual ~Enemy() = default;
    bool is_dead() override;
    bool is_inactive(bool arg_check_reversed = true) override;
};

class Friend : public ::cw::character::Character {
public:
    Friend() = default;
    virtual ~Friend() = default;
    void set_vanished();
};

class Player : public ::cw::character::Character {
public:
    Player() = default;
    virtual ~Player() = default;
    std::vector<std::shared_ptr<::cw::header::CardHeader>> m_personal_pocket{};
    void initialize(std::shared_ptr<::cw::data::CWPyElementTree> arg_data);
    bool add_personalpocket(std::shared_ptr<::cw::header::CardHeader> arg_header, ::cw::lib::Integer arg_index);
    bool _add_personalpocket(std::shared_ptr<::cw::header::CardHeader> arg_header, ::cw::lib::Integer arg_index);
    void remove_personalpocket(std::shared_ptr<::cw::header::CardHeader> arg_header);
    void store_personalpocket();
    void restore_personalpocket(bool arg_sort);
    void refresh_personalpocket(std::map<::cw::lib::String, std::shared_ptr<::cw::header::CardHeader>>& arg_personalcard_tbl);
    ::cw::lib::Integer get_personalpocketspace();
    ::cw::lib::Integer _get_personalpocketspace();
    void replace_personalcardposition(std::shared_ptr<::cw::header::CardHeader> arg_header1, std::shared_ptr<::cw::header::CardHeader> arg_header2);
    void update_personalownerindex(::cw::lib::Integer arg_index);
    void lost() override;
    void set_name(::cw::lib::String arg_name) override;
};

// character.pyの_couponlockに対応する、共有の再入不可ロック。
extern std::mutex g__couponlock;

bool Character_find_keycode_match(::cw::lib::String arg_keycode, std::vector<::cw::lib::String> arg_keycodes);
::cw::lib::Value Character_get_enhance_def_lambda_line_1864(::cw::lib::Value arg_a, ::cw::lib::Value arg_b);
::cw::lib::Value Character_get_enhance_def_lambda_line_1867(::cw::lib::Value arg_a, ::cw::lib::Value arg_b);
::cw::lib::Integer Character__get_enhance_impl_wrap_enhval(::cw::lib::Integer arg_val, ::cw::lib::Integer arg_orig_val);
::cw::lib::Integer calc_maxlife(::cw::lib::Real arg_vit, ::cw::lib::Real arg_minval, ::cw::lib::Integer arg_level);
::cw::lib::Real calc_lifecoefficient(std::shared_ptr<::cw::data::CWPyElementTree> arg_data, ::cw::lib::Integer arg_level, ::cw::lib::Integer arg_maxlife, ::cw::lib::Integer arg_vit, ::cw::lib::Integer arg_minval);
void main();
}

