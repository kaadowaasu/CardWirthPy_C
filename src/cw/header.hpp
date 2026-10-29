#pragma once

// cw/header.pyのC++20移植。実行時の画像・保存
// 処理は、 SDLビルドと共用する移植可能なアダプターを通す。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"

namespace cw::header {
// sqlite3.Rowと行イテレーターに対応する型付きの変換用オブジェクト。
// SDLビルドではsqlite3を公開せず、header.pyが使う全列とNULL
// ・バイト列の値を 保持したまま、呼び出し元がDBの列をこれらへ包んで渡せる。
struct DbRecord : ::cw::lib::Object {
    std::map<::cw::lib::String, ::cw::lib::Value> fields{};
};
struct DbRecordList : ::cw::lib::Object {
    std::vector<std::shared_ptr<DbRecord>> rows{};
};

class AdventurerHeader : public ::cw::lib::Object {
public:
    AdventurerHeader() = default;
    virtual ~AdventurerHeader() = default;
    ::cw::lib::Value m_age{};
    bool m_album{};
    ::cw::lib::Value m_desc{};
    ::cw::lib::Integer m_ep{};
    ::cw::lib::Value m_fpath{};
    std::shared_ptr<::cw::header::Gene> m_gene{};
    std::vector<::cw::lib::Value> m_history{};
    ::cw::lib::Value m_imgpaths{};
    bool m_leavenoalbum{};
    ::cw::lib::Value m_level{};
    bool m_lost{};
    ::cw::lib::String m_name{};
    ::cw::lib::Value m_order{};
    ::cw::lib::String m_race{};
    ::cw::lib::Value m_sex{};
    ::cw::lib::Value m_versionhint{};
    ::cw::lib::String m_wsnversion{};
    void initialize(std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_data, bool arg_album, std::optional<::cw::lib::Value> arg_dbrec, std::optional<std::shared_ptr<::cw::lib::SqlCursor>> arg_imgdbrec, ::cw::lib::String arg_fpath, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs);
    void made_baby();
    struct ParenthoodState {
        ::cw::lib::Integer ep{}, gene_count{};
        std::shared_ptr<::cw::data::CWPyElementTree> xml;
    };
    // Python版に加えてC++のトランザクションを保護するため、 保留中
    // のXMLをDBヘッダーのキャッシュ済みカウンターとは分けて保持する。
    ParenthoodState capture_parenthood_state();
    void restore_parenthood_state(const ParenthoodState& state);
    void grow();
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> get_imgpaths();
    ::cw::lib::String get_age();
    ::cw::lib::String get_sex();
    std::shared_ptr<::cw::header::RaceHeader> get_race();
};

class CardHeader : public ::cw::lib::Object, public std::enable_shared_from_this<CardHeader> {
public:
    CardHeader() = default;
    virtual ~CardHeader() = default;
    inline static ::cw::lib::String s_author{};
    inline static bool s_clickedflag{};
    inline static ::cw::lib::Integer s_deal_per{};
    inline static ::cw::lib::String s_name{};
    inline static bool s_negaflag{};
    inline static bool s_penalty{};
    inline static bool s_recycle{};
    inline static ::cw::lib::String s_scedir{};
    inline static ::cw::lib::String s_scenario{};
    inline static bool s_scenariocard{};
    inline static ::cw::lib::Integer s_star{};
    inline static std::shared_ptr<::cw::lib::Object> s_subrect{};
    inline static std::tuple<::cw::lib::Integer, ::cw::lib::Integer> s_textpos{};
    inline static std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>> s_versionhint{};
    inline static ::cw::lib::String s_wsnversion{};
    inline static std::shared_ptr<::cw::lib::Object> s_wxrect{};
    ::cw::lib::Value m__bordering_cardname{};
    std::optional<std::shared_ptr<::cw::image::CardImage>> m__cardimg{};
    ::cw::lib::Value m__cardscale{};
    // 呼び出し中はEngineを所有し、キャッシュされたEngineのイベントはこのヘッダーを所有
    // する。 Pythonには循環回収があるが、C++では逆向きのキャッシュ参照を弱参照にする。
    std::weak_ptr<::cw::event::EventEngine> m__events{};
    std::optional<std::shared_ptr<::cw::data::CWPyElementTree>> m__lazy_write{};
    bool m_lazy_write_needs_unique_path{false};
    ::cw::lib::Value m__show_premiumicon{};
    ::cw::lib::Value m__skindirname{};
    ::cw::lib::Value m__wxcardscale{};
    bool m_allrange{};
    bool m_attachment{};
    ::cw::lib::String m_author{};
    ::cw::lib::Value m_bgtype{};
    ::cw::lib::Value m_carddata{};
    bool m_clickedflag{};
    ::cw::lib::Value m_comment{};
    ::cw::lib::Integer m_deal_per{};
    ::cw::lib::String m_desc{};
    ::cw::lib::Integer m_enhance_avo{};
    ::cw::lib::Integer m_enhance_avo_used{};
    ::cw::lib::Integer m_enhance_def{};
    ::cw::lib::Integer m_enhance_def_used{};
    ::cw::lib::Integer m_enhance_res{};
    ::cw::lib::Integer m_enhance_res_used{};
    ::cw::lib::ValueMap m_flags{};
    ::cw::lib::String m_fpath{};
    bool m_hold{};
    ::cw::lib::Integer m_id{};
    ::cw::lib::Value m_imgpaths{};
    std::vector<::cw::lib::String> m_keycodes{};
    ::cw::lib::Integer m_level{};
    ::cw::lib::Integer m_maxuselimit{};
    ::cw::lib::Value m_mental{};
    ::cw::lib::Integer m_moved{};
    ::cw::lib::String m_name{};
    bool m_need_resetvariables{};
    bool m_negaflag{};
    ::cw::lib::Value m_order{};
    bool m_penalty{};
    // PCが自分のカードを所有する。逆参照で解放済みのPCを生かし続け
    // ないようにし、 読む側は操作の間だけ弱参照を所有参照へ変換する。
    std::weak_ptr<::cw::sprite::card::PlayerCard> m_personal_owner{};
    std::tuple<::cw::lib::Value, ::cw::lib::Value> m_personal_owner_index{};
    ::cw::lib::Value m_physical{};
    ::cw::lib::String m_premium{};
    ::cw::lib::Integer m_price{};
    ::cw::lib::Value m_rect{};
    bool m_recycle{};
    ::cw::lib::String m_scedir{};
    ::cw::lib::String m_scenario{};
    bool m_scenariocard{};
    ::cw::lib::Integer m_star{};
    ::cw::lib::ValueMap m_steps{};
    ::cw::lib::Value m_subrect{};
    ::cw::lib::String m_target{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> m_textpos{};
    ::cw::lib::String m_type{};
    ::cw::lib::Integer m_type_id{};
    ::cw::lib::Integer m_uselimit{};
    ::cw::lib::ValueMap m_variants{};
    ::cw::lib::Value m_versionhint{};
    std::tuple<::cw::lib::Value, ::cw::lib::Value> m_vocation{};
    ::cw::lib::Integer m_vocation_for_sort{};
    ::cw::lib::Value m_wsnversion{};
    ::cw::lib::Value m_wxrect{};
    void initialize(std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_data, std::optional<std::shared_ptr<::cw::character::Character>> arg_owner, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_carddata, bool arg_from_scenario, ::cw::lib::String arg_scedir, bool arg_put_db, std::optional<::cw::lib::Value> arg_dbrec, std::optional<std::shared_ptr<::cw::lib::SqlCursor>> arg_imgdbrec, ::cw::lib::String arg_dbowner, ::cw::lib::String arg_bgtype);
    // 名前を追加する前の元のキーコードを使い、XML・DBからの読み込みを完了する。
    void finalize_loaded_fields();
    ::cw::lib::Integer get_negastar();
    ::cw::lib::String get_showingname();
    void set_cardimg(std::vector<std::shared_ptr<::cw::image::ImageInfo>> arg_imgpaths, bool arg_can_loaded_scaledimage, bool arg_anotherscenariocard);
    void update_scenariopath(::cw::lib::String arg_normpath, ::cw::lib::String arg_dst);
    std::optional<::cw::lib::Value> get_owner();
    void set_owner(std::optional<::cw::lib::Value> arg_owner);
    std::shared_ptr<::cw::character::Character> get_owner_character() const;
    std::shared_ptr<::cw::header::CardHeader> get_original() const;
    ::cw::lib::String get_bgtype();
    std::shared_ptr<::cw::image::CardImage> get_cardimg();
    std::shared_ptr<::cw::lib::Bitmap> get_cardwxbmp(std::optional<std::shared_ptr<::cw::sprite::card::PlayerCard>> arg_test_aptitude);
    std::shared_ptr<::cw::lib::Surface> get_cardimg_2();
    void set_resetvariables(bool arg_resetvariables);
    void do_write(bool arg_dupcheck);
    ::cw::lib::Integer get_vocation_level(std::shared_ptr<::cw::character::Character> arg_owner, bool arg_enhance_act);
    ::cw::lib::Integer get_showed_vocation_level(std::shared_ptr<::cw::character::Character> arg_owner);
    ::cw::lib::Integer get_vocation_val(std::shared_ptr<::cw::character::Character> arg_owner, bool arg_enhance_act);
    void set_testaptitude(std::optional<std::shared_ptr<::cw::character::Character>> arg_test_aptitude);
    ::cw::lib::Integer get_uselimit_level();
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> get_uselimit(bool arg_reset);
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> get_enhance_val_used();
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> get_enhance_val();
    void set_uselimit(::cw::lib::Integer arg_value, bool arg_animate);
    void write(std::optional<std::shared_ptr<::cw::data::Party>> arg_party, bool arg_move, bool arg_from_getcontent);
    void contain_xml(bool arg_load);
    void remove_importedmaterials();
    void set_scenariostart();
    void set_scenarioend();
    void reset_variables();
    void initialize_variables(::cw::lib::String arg_initialization);
    bool can_write_variables();
    std::shared_ptr<::cw::header::CardHeader> copy();
    bool is_ccardheader();
    bool is_backpackheader();
    bool is_storehouseheader();
    bool is_hold();
    bool is_autoselectable();
    std::tuple<std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>>, std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>>> get_targets();
    bool is_noeffect(std::shared_ptr<::cw::character::Character> arg_target);
    std::vector<::cw::lib::String> get_keycodes(bool arg_with_name);
    std::optional<std::shared_ptr<::cw::event::EventEngine>> get_events();
    ::cw::lib::Integer find_keycode_position_excluding_empty(::cw::lib::Callback arg_matcher, ::cw::lib::Integer arg_startindex);
    ::cw::lib::String get_keycode_at_excluding_empty(::cw::lib::Integer arg_index);
    void set_hold(bool arg_hold);
    void set_star(::cw::lib::Integer arg_star);
    ::cw::lib::Integer get_sellingprice();
    bool can_selling();
    bool is_removewithstatus(::cw::lib::Value arg_ccard);
    bool is_activewithstatus(std::shared_ptr<::cw::character::Character> arg_ccard);
    ::cw::lib::String get_showstyle();
    bool get_can_loaded_scaledimage();

private:
    std::vector<::cw::lib::String> nonempty_keycodes_snapshot();
    // Pythonと同様に弱参照で保持する。従来のValue形式の参照は
    // アクセサーで生成し、 自己循環やCharacter→Header→
    // Characterの循環で古いシナリオを保持し続けないようにする。
    std::weak_ptr<::cw::character::Character> m_owner_character_typed{};
    std::optional<::cw::lib::String> m_owner_name_typed{};
    std::weak_ptr<::cw::header::CardHeader> m_original_typed{};
};

class Gene : public ::cw::lib::Object {
public:
    Gene() = default;
    virtual ~Gene() = default;
    std::vector<::cw::lib::Integer> m_bits{};
    ::cw::lib::Integer m_count{};
    void initialize(std::optional<std::vector<::cw::lib::Integer>> arg_bits = std::nullopt, ::cw::lib::Integer arg_count = 0);
    ::cw::lib::String get_str();
    void set_str(::cw::lib::String arg_s, ::cw::lib::Integer arg_count = 0);
    void reverse_bit(::cw::lib::Integer arg_index);
    void set_bit(::cw::lib::Integer arg_index, ::cw::lib::Integer arg_value);
    void set_randombit();
    void set_talentbit(::cw::lib::String arg_talent, ::cw::lib::String arg_oldtalent = "");
    ::cw::lib::Integer count_bits();
    std::shared_ptr<::cw::header::Gene> reverse();
    std::shared_ptr<::cw::header::Gene> fusion(std::shared_ptr<::cw::header::Gene> arg_gene);
    std::shared_ptr<::cw::header::Gene> rotate_father();
    std::shared_ptr<::cw::header::Gene> rotate_mother();
};

class GetName : public ::cw::lib::Object {
public:
    GetName() = default;
    virtual ~GetName() = default;
    std::map<std::optional<::cw::lib::String>, std::map<::cw::lib::String, ::cw::lib::String>> m_attrs{};
    ::cw::lib::String m_name{};
    std::vector<::cw::lib::String> m_stack{};
    ::cw::lib::Value m_tagname{};
    void initialize(::cw::lib::String arg_fpath, ::cw::lib::String arg_tagname);
    void start_element(::cw::lib::String arg_name, std::map<::cw::lib::String, ::cw::lib::String> arg_attrs);
    void end_element(::cw::lib::String arg_name);
    void character_data(::cw::lib::String arg_data);
};

class GetProperty : public ::cw::lib::Object {
public:
    GetProperty() = default;
    virtual ~GetProperty() = default;
    std::map<std::optional<::cw::lib::String>, std::map<::cw::lib::String, ::cw::lib::String>> m_attrs{};
    std::map<::cw::lib::String, ::cw::lib::String> m_properties{};
    std::vector<::cw::lib::String> m_stack{};
    ::cw::lib::ValueSet m_tag{};
    std::map<::cw::lib::String, std::vector<std::tuple<::cw::lib::String, std::map<::cw::lib::String, ::cw::lib::String>, ::cw::lib::String>>> m_third{};
    void initialize(::cw::lib::String arg_fpath, std::optional<::cw::lib::Value> arg_stream, std::optional<::cw::lib::Value> arg_tag);
    void start_element(::cw::lib::String arg_name, std::map<::cw::lib::String, ::cw::lib::String> arg_attrs);
    void end_element(::cw::lib::String arg_name);
    void character_data(::cw::lib::String arg_data);
};

class GetRootAttribute : public ::cw::lib::Object {
public:
    GetRootAttribute() = default;
    virtual ~GetRootAttribute() = default;
    std::map<::cw::lib::String, ::cw::lib::String> m_attrs{};
    void initialize(::cw::lib::String arg_fpath);
    void start_element(::cw::lib::String arg_name, std::map<::cw::lib::String, ::cw::lib::String> arg_attrs);
};

class InfoCardHeader : public ::cw::lib::Object {
public:
    InfoCardHeader() = default;
    virtual ~InfoCardHeader() = default;
    inline static ::cw::lib::String s_author{};
    inline static bool s_clickedflag{};
    inline static ::cw::lib::Integer s_deal_per{};
    inline static bool s_negaflag{};
    inline static ::cw::lib::String s_scenario{};
    inline static std::shared_ptr<::cw::lib::Object> s_wxrect{};
    ::cw::lib::Value m__bordering_cardname{};
    std::shared_ptr<::cw::image::CardImage> m__cardimg{};
    ::cw::lib::Value m__cardscale{};
    ::cw::lib::Value m__skindirname{};
    ::cw::lib::Value m__wxcardscale{};
    ::cw::lib::Value m_anotherscenariocard{};
    ::cw::lib::Value m_author{};
    ::cw::lib::Value m_can_loaded_scaledimage{};
    bool m_clickedflag{};
    ::cw::lib::Integer m_deal_per{};
    ::cw::lib::String m_desc{};
    ::cw::lib::Integer m_id{};
    ::cw::lib::Value m_imgpaths{};
    ::cw::lib::String m_name{};
    bool m_negaflag{};
    ::cw::lib::Value m_rect{};
    ::cw::lib::Value m_scenario{};
    ::cw::lib::Value m_wxrect{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data, bool arg_can_loaded_scaledimage);
    void set_cardimg(bool arg_can_loaded_scaledimage, bool arg_anotherscenariocard);
    std::shared_ptr<::cw::image::CardImage> get_cardimg();
    std::shared_ptr<::cw::lib::Bitmap> get_cardwxbmp(std::optional<std::shared_ptr<::cw::sprite::card::PlayerCard>> arg_test_aptitude);
    std::shared_ptr<::cw::lib::Surface> get_cardimg_2();
};

class PartyHeader : public ::cw::lib::Object {
public:
    PartyHeader() = default;
    virtual ~PartyHeader() = default;
    std::optional<std::vector<std::vector<::cw::lib::String>>> m__membercoupons{};
    std::optional<std::vector<::cw::lib::String>> m__memberdescs{};
    std::optional<std::vector<::cw::lib::Integer>> m__memberlevels{};
    std::optional<std::vector<::cw::lib::String>> m__membernames{};
    std::optional<std::vector<std::shared_ptr<::cw::header::GetProperty>>> m__memberprops{};
    std::optional<std::shared_ptr<::cw::data::Party>> m_data{};
    ::cw::lib::Value m_fpath{};
    std::vector<::cw::lib::Value> m_members{};
    ::cw::lib::Integer m_money{};
    ::cw::lib::String m_name{};
    ::cw::lib::Value m_order{};
    void initialize(std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_data, std::optional<::cw::lib::Value> arg_dbrec);
    bool is_adventuring();
    std::optional<std::shared_ptr<::cw::header::ScenarioHeader>> get_sceheader();
    // 解決済みのWSL（ZIPまたは展開済みディレクトリ）からメタデータを読む。
    static ::cw::lib::String read_scenario_path(const ::cw::lib::String& resolved_log);
    std::vector<::cw::lib::String> get_memberpaths(std::optional<::cw::lib::String> arg_yadodir);
    std::vector<std::shared_ptr<::cw::header::GetProperty>> _get_properties();
    std::vector<::cw::lib::String> get_membernames();
    std::vector<::cw::lib::String> get_memberdescs();
    std::vector<std::vector<::cw::lib::String>> get_membercoupons();
    std::vector<::cw::lib::Integer> get_memberlevels();
    ::cw::lib::Real get_average_level();
    ::cw::lib::Integer get_highest_level();
};

class PartyRecordHeader : public ::cw::lib::Object {
public:
    PartyRecordHeader() = default;
    virtual ~PartyRecordHeader() = default;
    ::cw::lib::Value m_backpack{};
    std::vector<::cw::lib::String> m_backpack_names{};
    ::cw::lib::Value m_fpath{};
    std::vector<::cw::lib::Value> m_membernames{};
    std::vector<::cw::lib::Value> m_members{};
    ::cw::lib::Integer m_money{};
    ::cw::lib::String m_name{};
    void initialize(std::optional<::cw::lib::String> arg_fpath, std::optional<::cw::lib::Value> arg_dbrec, std::optional<std::shared_ptr<::cw::thread::StoredParty>> arg_partyrecord);
    void rename_member(::cw::lib::String arg_fpath, ::cw::lib::String arg_name);
    void vanish_member(::cw::lib::String arg_fpath);
    std::vector<::cw::lib::String> get_memberpaths();
    std::vector<::cw::lib::String> get_membernames();
};

class RaceHeader : public ::cw::lib::Object {
public:
    RaceHeader() = default;
    virtual ~RaceHeader() = default;
    ::cw::lib::Real m_aggressive{};
    ::cw::lib::Integer m_agl{};
    bool m_automaton{};
    ::cw::lib::Integer m_avoid{};
    ::cw::lib::Real m_brave{};
    ::cw::lib::Real m_cautious{};
    ::cw::lib::Real m_cheerful{};
    std::optional<::cw::lib::Integer> m_coeff_ep{};
    std::optional<::cw::lib::Real> m_coeff_level{};
    bool m_constructure{};
    std::vector<::cw::lib::Value> m_coupons{};
    ::cw::lib::Integer m_defense{};
    ::cw::lib::String m_desc{};
    ::cw::lib::Integer m_dex{};
    ::cw::lib::Integer m_py_int{};
    ::cw::lib::Integer m_min{};
    ::cw::lib::String m_name{};
    bool m_noeffect_magic{};
    bool m_noeffect_weapon{};
    ::cw::lib::Integer m_resist{};
    bool m_resist_fire{};
    bool m_resist_ice{};
    ::cw::lib::Integer m_str{};
    ::cw::lib::Real m_trickish{};
    bool m_undead{};
    bool m_unholy{};
    ::cw::lib::Integer m_vit{};
    bool m_weakness_fire{};
    bool m_weakness_ice{};
    std::vector<std::tuple<::cw::lib::String, ::cw::lib::Integer>> m_coupon_entries{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
};

class SavedJPDCImageHeader : public ::cw::lib::Object {
public:
    SavedJPDCImageHeader() = default;
    virtual ~SavedJPDCImageHeader() = default;
    ::cw::lib::Value m_dpath{};
    ::cw::lib::Value m_fpath{};
    std::vector<::cw::lib::Value> m_fpaths{};
    ::cw::lib::String m_scenarioauthor{};
    ::cw::lib::String m_scenarioname{};
    std::vector<::cw::lib::String> m_fpaths_typed{};
    void initialize(std::optional<::cw::lib::String> arg_fpath, std::optional<::cw::lib::Value> arg_dbrec);
    static std::shared_ptr<::cw::header::SavedJPDCImageHeader> create_header(std::optional<std::shared_ptr<::cw::debug::logging::DebugLog>> arg_debuglog);
    void remove_all();
};

class ScenarioHeader : public ::cw::lib::Object {
public:
    ScenarioHeader() = default;
    virtual ~ScenarioHeader() = default;
    std::optional<std::vector<std::shared_ptr<::cw::image::ImageInfo>>> m__imginfos{};
    std::optional<::cw::lib::Real> m__up_scr{};
    std::optional<::cw::lib::Real> m__up_win{};
    std::optional<std::vector<std::shared_ptr<::cw::lib::Bitmap>>> m__wxbmps{};
    std::optional<std::vector<std::shared_ptr<::cw::lib::Bitmap>>> m__wxbmps_noscale{};
    ::cw::lib::Value m_author{};
    ::cw::lib::Value m_coupons{};
    ::cw::lib::Value m_couponsnum{};
    ::cw::lib::Value m_ctime{};
    ::cw::lib::Value m_desc{};
    ::cw::lib::String m_dpath{};
    ::cw::lib::String m_fname{};
    std::map<::cw::lib::Integer, std::vector<std::optional<::cw::lib::Bytes>>> m_images{};
    std::vector<::cw::lib::Value> m_imgpaths{};
    ::cw::lib::Value m_levelmax{};
    ::cw::lib::Value m_levelmin{};
    ::cw::lib::Value m_mtime{};
    ::cw::lib::Value m_name{};
    std::optional<::cw::lib::String> m_skindir{};
    ::cw::lib::Value m_skintype{};
    ::cw::lib::Value m_startid{};
    ::cw::lib::Value m_tags{};
    ::cw::lib::Value m_type{};
    ::cw::lib::Value m_wsnversion{};
    // SQLite Row相当の値を受け取れないビルド構成でも、読み出した列を
    // 型付きで保持して後段の画像／一覧処理から利用できるようにする。
    std::map<::cw::lib::String, ::cw::lib::Value> m_dbrec_fields{};
    std::vector<std::map<::cw::lib::String, ::cw::lib::Value>> m_imgdbrec_typed{};
    void initialize(::cw::lib::Value arg_dbrec, std::optional<std::vector<std::map<::cw::lib::String, ::cw::lib::Value>>> arg_imgdbrec);
    ::cw::lib::Value get_mtime_reversed();
    ::cw::lib::String get_fpath();
    void set_fpath(::cw::lib::String arg_fpath);
    std::tuple<std::vector<std::shared_ptr<::cw::lib::Bitmap>>, std::vector<std::shared_ptr<::cw::lib::Bitmap>>, std::vector<std::shared_ptr<::cw::image::ImageInfo>>> get_wxbmps(bool arg_mask);
    std::optional<std::tuple<std::vector<std::shared_ptr<::cw::lib::Surface>>, std::vector<std::shared_ptr<::cw::image::ImageInfo>>>> get_bmps(bool arg_mask, std::optional<::cw::lib::Real> arg_up_scr);
};

class UnknownRaceHeader : public ::cw::header::RaceHeader {
public:
    UnknownRaceHeader() = default;
    virtual ~UnknownRaceHeader() = default;
    // Pythonの種族属性は一組だけなので、RaceHeaderのフィールドを使う。
    // 基底クラスのポインターからも、初期化された6種類の身体能力を参照できるようにする。
    void initialize(std::shared_ptr<::cw::setting::Setting> arg_setting);
};

extern ::cw::lib::Value g_sqlite3_Row;

std::vector<std::shared_ptr<::cw::image::ImageInfo>> to_imgpaths(::cw::lib::Value arg_dbrec, std::optional<std::shared_ptr<::cw::lib::SqlCursor>> arg_imgdbrec);
::cw::lib::Integer cardtype_to_pocket(::cw::lib::String arg_ctype);
::cw::lib::String CardHeader_write_create_newpath(std::optional<std::shared_ptr<::cw::data::Party>> arg_party, ::cw::lib::String arg_cardtype, ::cw::lib::String arg_name);
bool is_removewithstatus(std::shared_ptr<::cw::data::CWPyElement> arg_carddata, ::cw::lib::Value arg_target);
::cw::lib::String enforce_str(::cw::lib::Value arg_value);
std::optional<::cw::lib::String> enforce_optional_str(::cw::lib::Value arg_value);
::cw::lib::Integer enforce_int(::cw::lib::Value arg_value);
::cw::lib::Real enforce_float(::cw::lib::Value arg_value);
::cw::lib::Real enforce_float_or_int(::cw::lib::Value arg_value);
bool enforce_bool(::cw::lib::Value arg_value);
::cw::lib::Bytes enforce_bytes(::cw::lib::Value arg_value);
std::optional<::cw::lib::Bytes> enforce_optional_bytes(::cw::lib::Value arg_value);
void main();
}

