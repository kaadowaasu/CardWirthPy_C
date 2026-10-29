#pragma once
#include "cw/lib/xml_attributes.hpp"

// cw/data.pyに対応する、XML
// 要素とアプリケーションデータの宣言。
#include "cw/lib/compat.hpp"
#include "cw/lib/money.hpp"
#include "cw/fwd.hpp"
#include <algorithm>
#include <stdexcept>

namespace cw::data {
// XML読み込みと直接のバイナリー変換で共用する、Python互換の数値構文。
::cw::lib::Real parse_python_float(const ::cw::lib::String& value);
::cw::lib::Integer parse_python_integer_float(const ::cw::lib::String& value);

// Pythonのゴシップの取り消し辞書は、ログの保存・再読み込み後も挿入順を保つ
// 。 キーで並べ替えるmapを使うと、F9で再追加される順序が変わってしまう。
class GossipJournal {
    using Entries = std::vector<std::pair<::cw::lib::String, bool>>;
    Entries entries_;
public:
    auto begin() { return entries_.begin(); }
    auto end() { return entries_.end(); }
    auto begin() const { return entries_.begin(); }
    auto end() const { return entries_.end(); }
    auto find(const ::cw::lib::String& key) {
        return std::find_if(entries_.begin(), entries_.end(), [&](const auto& entry) { return entry.first == key; });
    }
    auto find(const ::cw::lib::String& key) const {
        return std::find_if(entries_.begin(), entries_.end(), [&](const auto& entry) { return entry.first == key; });
    }
    bool contains(const ::cw::lib::String& key) const { return find(key) != end(); }
    bool& at(const ::cw::lib::String& key) {
        const auto position = find(key);
        if (position == end()) throw std::out_of_range("gossip journal key");
        return position->second;
    }
    const bool& at(const ::cw::lib::String& key) const {
        const auto position = find(key);
        if (position == end()) throw std::out_of_range("gossip journal key");
        return position->second;
    }
    bool operator==(const GossipJournal& other) const {
        // Pythonのdictや従来のstd::mapと同様に、等価比較では
        // 順序を無視する。 反復処理では最初の挿入順を保ち、F9の復元に使う。
        if (entries_.size() != other.entries_.size()) return false;
        for (const auto& [key, value] : entries_) {
            const auto position = other.find(key);
            if (position == other.end() || position->second != value) return false;
        }
        return true;
    }
    void clear() { entries_.clear(); }
    void erase(Entries::iterator position) { entries_.erase(position); }
    bool& operator[](const ::cw::lib::String& key) {
        if (const auto position = find(key); position != end()) return position->second;
        entries_.emplace_back(key, false);
        return entries_.back().second;
    }
};

class CacheData : public ::cw::lib::Object {
public:
    CacheData() = default;
    virtual ~CacheData() = default;
    ::cw::lib::Value m_data{};
    ::cw::lib::Value m_mtime{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data, ::cw::lib::Real arg_mtime);
};

class EndTargetTagException : public ::cw::lib::Object {
public:
    EndTargetTagException() = default;
    virtual ~EndTargetTagException() = default;
};

class Flag : public ::cw::lib::Object, public std::enable_shared_from_this<Flag> {
public:
    Flag() = default;
    virtual ~Flag() = default;
    std::shared_ptr<::cw::data::CWPyElement> m__data{};
    ::cw::lib::Value m__parent{};
    bool m_defaultvalue{};
    ::cw::lib::String m_falsename{};
    std::vector<::cw::lib::String> m_initialization{};
    bool m_is_writable{};
    ::cw::lib::String m_name{};
    bool m_spchars{};
    ::cw::lib::String m_truename{};
    bool m_value{};
    void initialize(std::optional<::cw::lib::Value> arg_parent, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_data, bool arg_value, ::cw::lib::String arg_name, ::cw::lib::String arg_truename, ::cw::lib::String arg_falsename, bool arg_defaultvalue, bool arg_spchars);
    bool to_bool();
    void redraw_cards(::cw::lib::Integer arg_cardspeed, bool arg_overridecardspeed);
    void set(bool arg_value, bool arg_updatedebugger = true);
    void reverse();
    ::cw::lib::String get_valuename(std::optional<bool> arg_value);
    void write_value();
};

class Party : public ::cw::lib::Object, public std::enable_shared_from_this<Party> {
public:
    Party() = default;
    virtual ~Party() = default;
    inline static std::vector<std::shared_ptr<::cw::header::CardHeader>> s_backpack{};
    inline static bool s_is_suspendlevelup{};
    inline static std::vector<std::shared_ptr<::cw::data::CWPyElementTree>> s_members{};
    inline static ::cw::lib::Integer s_money{};
    inline static ::cw::lib::String s_name{};
    inline static bool s_sorted_backpack_by_order{};
    bool m__loading{};
    std::vector<std::shared_ptr<::cw::header::CardHeader>> m_backpack{};
    std::vector<std::shared_ptr<::cw::header::CardHeader>> m_backpack_moved{};
    ::cw::lib::Value m_data{};
    bool m_is_suspendlevelup{};
    std::vector<::cw::lib::Value> m_lastscenario{};
    ::cw::lib::Value m_lastscenariopath{};
    std::vector<::cw::lib::Value> m_members{};
    ::cw::lib::Integer m_money{};
    ::cw::lib::String m_name{};
    ::cw::lib::Value m_partyinfoonly{};
    ::cw::lib::Value m_path{};
    bool m_sorted_backpack_by_order{};
    std::vector<std::shared_ptr<::cw::sprite::card::PlayerCard>> m_vanished_pcards{};
    // Pyは逆変換中に一時的なグローバル宿コンテキストを使う。 C++では
    // UI・ゲームスレッドの競合を避けるため、Partyごとに保持する。
    ::cw::lib::String m_read_yadodir{}, m_read_tempdir{};
    void initialize(std::shared_ptr<::cw::header::PartyHeader> arg_header, bool arg_partyinfoonly,
        ::cw::lib::String read_yadodir={}, ::cw::lib::String read_tempdir={});
    void _init(std::shared_ptr<::cw::header::PartyHeader> arg_header, bool arg_partyinfoonly);
    ::cw::lib::String get_showingname();
    void sort_backpack(std::optional<::cw::lib::String> arg_sorttype, std::optional<std::shared_ptr<::cw::character::Character>> arg_test_aptitude);
    std::optional<std::shared_ptr<::cw::header::CardHeader>> find_keycode(::cw::lib::String arg_keycode, bool arg_skill, bool arg_item, bool arg_beast, bool arg_hand, ::cw::lib::String arg_condition);
    ::cw::lib::String get_relpath();
    ::cw::lib::String get_yadodir();
    ::cw::lib::String get_tempdir();
    bool is_loading();
    void reload();
    bool add(std::shared_ptr<::cw::header::AdventurerHeader> arg_header, std::optional<std::shared_ptr<::cw::data::CWPyElementTree>> arg_data);
    void remove(std::shared_ptr<::cw::sprite::card::PlayerCard> arg_pcard);
    void replace_order(::cw::lib::Integer arg_index1, ::cw::lib::Integer arg_index2);
    void set_name(::cw::lib::String arg_name);
    void set_money(::cw::lib::Integer arg_value, bool arg_fromevent, bool arg_blink);
    void suspend_levelup(bool arg_suspend);
    void set_numbercoupon();
    void remove_numbercoupon();
    void write();
    void lost();
    std::map<::cw::lib::String, ::cw::lib::Integer> get_coupontable();
    std::set<::cw::lib::String> get_coupons();
    std::vector<std::shared_ptr<::cw::header::CardHeader>> get_allcardheaders();
    bool is_adventuring();
    std::optional<std::shared_ptr<::cw::header::ScenarioHeader>> get_sceheader();
    std::vector<::cw::lib::String> get_memberpaths();
    // 読み込んだメンバーのパスをパーティ順に返す。未保存・一時保存のパスも含む。
    std::vector<::cw::lib::String> get_loaded_memberpaths() const;
    void set_lastscenario(std::vector<::cw::lib::String> arg_lastscenario, ::cw::lib::String arg_lastscenariopath);
private:
    void sync_member_references();
};

class SimpleXmlParser : public ::cw::lib::Object {
public:
    SimpleXmlParser() = default;
    virtual ~SimpleXmlParser() = default;
    bool m__persed{};
    std::vector<::cw::lib::String> m_currenttags{};
    ::cw::lib::Value m_file{};
    ::cw::lib::Value m_fpath{};
    std::vector<std::shared_ptr<::cw::data::CWPyElement>> m_node_stack{};
    std::vector<::cw::lib::String> m_parsetags{};
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> m_root{};
    ::cw::lib::Value m_rootattrs{};
    // Python版のrootattrsは解析時にルート属性を呼出し元の辞書へ書き戻す。
    // compat::Valueに再帰的な辞書型を持たせず、C++呼出し境界では型付きで保持する。
    std::optional<std::map<::cw::lib::String, ::cw::lib::String>> m_rootattrs_typed{};
    ::cw::lib::Value m_targetonly{};
    ::cw::lib::Value m_targettag{};
    void initialize(::cw::lib::String arg_fpath, ::cw::lib::String arg_targettag, std::optional<::cw::lib::Value> arg_stream, bool arg_targetonly, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs);
    void initialize(::cw::lib::String arg_fpath, ::cw::lib::String arg_targettag, std::optional<::cw::lib::Value> arg_stream, bool arg_targetonly, std::map<::cw::lib::String, ::cw::lib::String>& arg_rootattrs);
    void _clear_attrs();
    void start_element(::cw::lib::String arg_name, ::cw::lib::XmlAttributes arg_attrs);
    void end_element(::cw::lib::String arg_name);
    void char_data(::cw::lib::String arg_data);
    std::shared_ptr<::cw::data::CWPyElement> parse();
    void parse_file(::cw::lib::Value arg_fname);
    ::cw::lib::Value _create_parser();
    void _parse_file(::cw::lib::Value arg_fname);
    ::cw::lib::String get_currentpath();
};

class Step : public ::cw::lib::Object, public std::enable_shared_from_this<Step> {
public:
    Step() = default;
    virtual ~Step() = default;
    std::shared_ptr<::cw::data::CWPyElement> m__data{};
    ::cw::lib::Value m__parent{};
    ::cw::lib::Integer m_defaultvalue{};
    std::vector<::cw::lib::String> m_initialization{};
    bool m_is_writable{};
    ::cw::lib::String m_name{};
    bool m_spchars{};
    ::cw::lib::Integer m_value{};
    std::vector<::cw::lib::String> m_valuenames{};
    void initialize(std::optional<::cw::lib::Value> arg_parent, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_data, ::cw::lib::Integer arg_value, ::cw::lib::String arg_name, std::vector<::cw::lib::String> arg_valuenames, ::cw::lib::Integer arg_defaultvalue, bool arg_spchars);
    void set(::cw::lib::Integer arg_value, bool arg_updatedebugger = true);
    void up();
    void down();
    ::cw::lib::String get_valuename(std::optional<::cw::lib::Integer> arg_value);
    void write_value();
};

// VariantでPythonのlist値を表す動的配列。Value
// 自体は小さなスカラーの選択型とし、 再帰的な内容はObject
// 派生の保持型へ分離する。 compat::Value
// を自己再帰型にせず、XMLの値全体を保持する。
class VariantSequence : public ::cw::lib::Object {
public:
    VariantSequence() = default;
    virtual ~VariantSequence() = default;
    std::vector<::cw::lib::Value> m_values{};
};

// Pythonと同様に、辞書の保持型はStructValのオブジェクト属性と区別する。
class VariantMapping : public ::cw::lib::Object {
public:
    ::cw::lib::ValueMap m_values{};
};

class StructVal : public ::cw::lib::Object {
public:
    StructVal() = default;
    virtual ~StructVal() = default;
    ::cw::lib::Value m_members{};
    ::cw::lib::Value m_name{};
    std::vector<::cw::lib::Value> m_member_values{};
    std::vector<::cw::lib::String> m_member_names{};
    void initialize(::cw::lib::String arg_name, std::vector<::cw::lib::Value> arg_members);
};

class SystemData : public ::cw::lib::Object {
public:
    SystemData() = default;
    virtual ~SystemData() = default;
    inline static std::map<::cw::lib::String, std::vector<std::optional<::cw::lib::Value>>> s_ex_cache{};
    inline static ::cw::lib::ValueMap s_resource_cache{};
    std::map<::cw::lib::Integer, std::tuple<::cw::lib::String, ::cw::lib::String>> m__areas{};
    std::map<::cw::lib::Integer, std::tuple<::cw::lib::String, ::cw::lib::String>> m__battles{};
    std::map<::cw::lib::Integer, std::tuple<::cw::lib::String, ::cw::lib::String>> m__beasts{};
    std::map<::cw::lib::Integer, std::tuple<::cw::lib::String, ::cw::lib::String>> m__casts{};
    bool m__fastLoadCache{};
    ::cw::lib::Integer m__filenum{};
    std::map<::cw::lib::Integer, std::shared_ptr<::cw::header::InfoCardHeader>> m__infocard_cache{};
    std::map<::cw::lib::Integer, std::tuple<::cw::lib::String, ::cw::lib::String>> m__infos{};
    std::map<::cw::lib::Integer, std::tuple<::cw::lib::String, ::cw::lib::String>> m__items{};
    std::map<::cw::lib::Integer, std::tuple<::cw::lib::String, ::cw::lib::String>> m__packs{};
    ::cw::lib::Real m__paused_time{};
    // Pyは別のシナリオのタイマーを破棄する場合だけ整数の0を使う
    // 。 内部のReal型とは独立して、XML上の整数表記を保つ。
    bool m__paused_time_integer_zero{};
    ::cw::lib::Value m__r_specialchar{};
    std::map<::cw::lib::Integer, std::tuple<::cw::lib::String, ::cw::lib::String>> m__skills{};
    ::cw::lib::String m_author{};
    bool m_autostart_round{};
    std::map<::cw::lib::String, std::tuple<::cw::lib::String, ::cw::lib::Real>> m_background_image_mtime{};
    std::vector<::cw::lib::Value> m_backlog{};
    std::set<::cw::lib::String> m_breakpoints{};
    bool m_can_loaded_scaledimage{};
    std::map<::cw::lib::String, bool> m_compstamps{};
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> m_data{};
    std::map<::cw::lib::String, std::shared_ptr<::cw::data::CacheData>> m_data_cache{};
    std::optional<std::shared_ptr<::cw::debug::logging::DebugLog>> m_debuglog{};
    ::cw::lib::Value m_deletedpaths{};
    std::optional<std::shared_ptr<::cw::event::EventEngine>> m_events{};
    ::cw::lib::ValueMap m_ex_cache{};
    std::map<::cw::lib::String, std::shared_ptr<::cw::data::Flag>> m_flags{};
    ::cw::lib::String m_fpath{};
    std::vector<std::shared_ptr<::cw::sprite::card::FriendCard>> m_friendcards{};
    GossipJournal m_gossips{};
    std::map<::cw::lib::String, ::cw::lib::String> m_ignorecase_table{};
    bool m_in_endprocess{};
    bool m_in_f9{};
    ::cw::lib::Integer m_infocard_maxindex{};
    std::vector<::cw::lib::Integer> m_infocards{};
    std::optional<std::set<::cw::lib::Integer>> m_infocards_beforeevent{};
    std::vector<::cw::lib::String> m_instructions{};
    bool m_is_playing{};
    std::map<::cw::lib::String, ::cw::lib::String> m_labels{};
    std::set<::cw::lib::String> m_lostadventurers{};
    std::map<std::tuple<::cw::lib::String, ::cw::lib::Integer>, std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>> m_moved_mcards{};
    ::cw::lib::Real m_mtime{};
    ::cw::lib::String m_name{};
    ::cw::lib::Integer m_notice_debuglog{};
    bool m_notice_infoview{};
    bool m_party_environment_backpack{};
    bool m_party_environment_gameover{};
    bool m_party_environment_runaway{};
    std::map<std::tuple<::cw::lib::String, ::cw::lib::Integer, bool, bool>, ::cw::lib::String> m_path_cache{};
    std::optional<std::shared_ptr<::cw::event::EventEngine>> m_playerevents{};
    std::optional<std::tuple<::cw::lib::Integer, std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>, std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>>> m_pre_battleareadata{};
    ::cw::lib::ValueMap m_resource_cache{};
    ::cw::lib::Integer m_resource_cache_size{};
    ::cw::lib::String m_scedir{};
    ::cw::lib::String m_sdata{};
    std::map<::cw::lib::Integer, std::vector<::cw::lib::Value>> m_sparea_mcards{};
    ::cw::lib::Value m_specialchars{};
    ::cw::lib::Integer m_startid{};
    std::map<::cw::lib::String, std::shared_ptr<::cw::data::Step>> m_steps{};
    std::optional<std::shared_ptr<::cw::data::CWPyElementTree>> m_summary{};
    ::cw::lib::String m_summarypath{};
    ::cw::lib::String m_tempdir{};
    std::map<std::tuple<std::shared_ptr<::cw::sprite::card::PlayerCard>, std::shared_ptr<::cw::header::CardHeader>>, ::cw::lib::Integer> m_uselimit_table{};
    std::map<::cw::lib::String, std::shared_ptr<::cw::data::Variant>> m_variants{};
    std::vector<std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>>> m_versionhint{};
    ::cw::lib::String m_wsn_version{};
    void initialize(bool arg_init);
    void _init_flags();
    void _init_steps();
    void _init_variants();
    virtual void save_variables();
    virtual void load_variables();
    void _load_variables(::cw::lib::Value arg_data);
    void reset_variables();
    void _init_debugger();
    virtual void update_skin();
    virtual void _init_xmlpaths(bool arg_xmlonly);
    void update_scenariopath(::cw::lib::String arg_normpath, ::cw::lib::String arg_dst, bool arg_dstisfile);
    void update_scenariopath2(::cw::lib::String arg_normpath, ::cw::lib::String arg_dst, bool arg_dstisfile);
    void _init_sparea_mcards();
    bool is_wsnversion(::cw::lib::String arg_wsn_version, std::optional<::cw::lib::String> arg_cardversion);
    virtual std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>> get_versionhint(::cw::lib::Integer arg_frompos);
    virtual void set_versionhint(::cw::lib::Integer arg_pos, std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>> arg_hint);
    virtual void update_scale();
    void sweep_resourcecache(::cw::lib::Integer arg_size);
    std::optional<std::shared_ptr<::cw::data::Flag>> find_flag(::cw::lib::String arg_path, bool arg_is_differentscenario, std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    std::optional<std::shared_ptr<::cw::data::Step>> find_step(::cw::lib::String arg_path, bool arg_is_differentscenario, std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    std::optional<std::shared_ptr<::cw::data::Variant>> find_variant(::cw::lib::String arg_path, bool arg_is_differentscenario, std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    bool get_flagvalue(::cw::lib::String arg_path);
    virtual void start();
    virtual void end(bool arg_showdebuglog, bool arg_complete, bool arg_failure);
    virtual void save_breakpoints();
    virtual ::cw::lib::Real get_totalpyaingtime();
    virtual ::cw::lib::Real get_pausedtime();
    virtual void start_timekeeper();
    virtual void resume_timekeeper();
    virtual void sleep_timekeeper();
    std::tuple<bool, std::optional<std::vector<std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, bool, ::cw::lib::String>>>> set_log(bool arg_force_create);
    virtual void create_log();
    void remove_log(std::optional<std::shared_ptr<::cw::debug::logging::DebugLog>> arg_debuglog);
    static void validate_recovery_log(const ::cw::lib::String& directory,
        const ::cw::lib::String& party_filename);
    virtual std::vector<std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, bool, ::cw::lib::String>> load_log(::cw::lib::String arg_path, bool arg_recording);
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> get_resdata(bool arg_isbattle, ::cw::lib::Integer arg_resid);
    virtual std::optional<std::shared_ptr<::cw::data::CWPyElement>> get_carddata(std::shared_ptr<::cw::data::CWPyElement> arg_linkdata, bool arg_in_inusecard, std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_inusecardheader);
    virtual bool is_updatedfilenames();
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> _get_resdata(const std::map<::cw::lib::Integer, std::tuple<::cw::lib::String, ::cw::lib::String>>& arg_table, ::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, ::cw::lib::String arg_resname, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs);
private:
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> _get_resdata_impl(const std::map<::cw::lib::Integer, std::tuple<::cw::lib::String, ::cw::lib::String>>& arg_table, ::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, ::cw::lib::String arg_resname, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs, std::map<::cw::lib::String, ::cw::lib::String>* output);
public:
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> _get_resdata(const std::map<::cw::lib::Integer, std::tuple<::cw::lib::String, ::cw::lib::String>>& arg_table, ::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, ::cw::lib::String arg_resname, std::map<::cw::lib::String, ::cw::lib::String>& arg_rootattrs);
    std::optional<::cw::lib::String> _get_resname(std::map<::cw::lib::Integer, std::tuple<::cw::lib::String, ::cw::lib::String>> arg_table, ::cw::lib::Integer arg_resid);
    std::optional<::cw::lib::String> _get_resfpath(const std::map<::cw::lib::Integer, std::tuple<::cw::lib::String, ::cw::lib::String>>& arg_table, ::cw::lib::Integer arg_resid);
    std::vector<::cw::lib::Integer> _get_resids(std::map<::cw::lib::Integer, std::tuple<::cw::lib::String, ::cw::lib::String>> arg_table);
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> get_areadata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs);
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> get_areadata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::map<::cw::lib::String, ::cw::lib::String>& arg_rootattrs);
    std::optional<::cw::lib::String> get_areaname(::cw::lib::Integer arg_resid);
    std::optional<::cw::lib::String> get_areafpath(::cw::lib::Integer arg_resid);
    std::vector<::cw::lib::Integer> get_areaids();
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> get_battledata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs);
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> get_battledata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::map<::cw::lib::String, ::cw::lib::String>& arg_rootattrs);
    std::optional<::cw::lib::String> get_battlename(::cw::lib::Integer arg_resid);
    std::optional<::cw::lib::String> get_battlefpath(::cw::lib::Integer arg_resid);
    std::vector<::cw::lib::Integer> get_battleids();
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> get_packagedata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs);
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> get_packagedata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::map<::cw::lib::String, ::cw::lib::String>& arg_rootattrs);
    std::optional<::cw::lib::String> get_packagename(::cw::lib::Integer arg_resid);
    std::optional<::cw::lib::String> get_packagefpath(::cw::lib::Integer arg_resid);
    std::vector<::cw::lib::Integer> get_packageids();
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> get_castdata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs);
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> get_castdata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::map<::cw::lib::String, ::cw::lib::String>& arg_rootattrs);
    std::optional<::cw::lib::String> get_castname(::cw::lib::Integer arg_resid);
    std::optional<::cw::lib::String> get_castfpath(::cw::lib::Integer arg_resid);
    std::vector<::cw::lib::Integer> get_castids();
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> get_skilldata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs);
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> get_skilldata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::map<::cw::lib::String, ::cw::lib::String>& arg_rootattrs);
    std::optional<::cw::lib::String> get_skillname(::cw::lib::Integer arg_resid);
    std::optional<::cw::lib::String> get_skillfpath(::cw::lib::Integer arg_resid);
    std::vector<::cw::lib::Integer> get_skillids();
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> get_itemdata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs);
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> get_itemdata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::map<::cw::lib::String, ::cw::lib::String>& arg_rootattrs);
    std::optional<::cw::lib::String> get_itemname(::cw::lib::Integer arg_resid);
    std::optional<::cw::lib::String> get_itemfpath(::cw::lib::Integer arg_resid);
    std::vector<::cw::lib::Integer> get_itemids();
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> get_beastdata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs);
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> get_beastdata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::map<::cw::lib::String, ::cw::lib::String>& arg_rootattrs);
    std::optional<::cw::lib::String> get_beastname(::cw::lib::Integer arg_resid);
    std::optional<::cw::lib::String> get_beastfpath(::cw::lib::Integer arg_resid);
    std::vector<::cw::lib::Integer> get_beastids();
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> get_infodata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs);
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> get_infodata(::cw::lib::Integer arg_resid, ::cw::lib::String arg_tag, bool arg_nocache, std::map<::cw::lib::String, ::cw::lib::String>& arg_rootattrs);
    std::optional<::cw::lib::String> get_infoname(::cw::lib::Integer arg_resid);
    std::optional<::cw::lib::String> get_infofpath(::cw::lib::Integer arg_resid);
    std::vector<::cw::lib::Integer> get_infoids();
    ::cw::lib::String _get_carddatapath(::cw::lib::String arg_rtype, ::cw::lib::Integer arg_resid, ::cw::lib::String arg_dpath);
    void copy_carddata(std::shared_ptr<::cw::data::CWPyElement> arg_linkdata, ::cw::lib::String arg_dstdir, bool arg_from_scenario, ::cw::lib::String arg_scedir, std::map<::cw::lib::String, ::cw::lib::String>& arg_imgpaths);
    virtual bool change_data(::cw::lib::Integer arg_resid, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_data);
    void start_event(std::optional<::cw::lib::Integer> arg_keynum, std::optional<std::vector<::cw::lib::String>> arg_keycodes, bool arg_redraw);
    void reload_variables();
    ::cw::lib::String get_currentareaname();
    std::vector<std::shared_ptr<::cw::data::CWPyElement>> get_bgdata(std::optional<std::vector<std::shared_ptr<::cw::data::CWPyElement>>> arg_e);
    std::tuple<::cw::lib::String, std::vector<std::shared_ptr<::cw::data::CWPyElement>>> get_mcarddata(std::optional<::cw::lib::Integer> arg_resid, std::optional<bool> arg_battlestatus, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_data);
    virtual std::vector<::cw::lib::String> get_bgmpaths();
    virtual void fullrecovery_fcards();
    bool has_infocards();
    void _tidying_infocards();
    std::vector<::cw::lib::Integer> get_infocards(bool arg_order);
    void append_infocard(::cw::lib::Integer arg_resid);
    void remove_infocard(::cw::lib::Integer arg_resid);
    bool has_infocard(::cw::lib::Integer arg_resid);
    ::cw::lib::Integer count_infocards();
    std::vector<std::shared_ptr<::cw::header::InfoCardHeader>> get_infocardheaders();
    bool can_gameover();
    void enableFastLoadCache();
    void disableFastLoadCache();
    bool fastLoadCacheEnabled();
private:
    // Pythonのdictと同様、後からSummaryを編集してもXMLの挿入順を保つ。
    std::vector<::cw::lib::String> m_flags_order{};
    std::vector<::cw::lib::String> m_steps_order{};
    std::vector<::cw::lib::String> m_variants_order{};
};

class SystemData_MEMORYSTATUSEX : public ::cw::lib::Object {
public:
    SystemData_MEMORYSTATUSEX() = default;
    virtual ~SystemData_MEMORYSTATUSEX() = default;
    inline static std::vector<std::tuple<::cw::lib::String, ::cw::lib::Value>> s__fields_{};
};

class Variant : public ::cw::lib::Object, public std::enable_shared_from_this<Variant> {
public:
    Variant() = default;
    virtual ~Variant() = default;
    ::cw::lib::Value m__data{};
    ::cw::lib::Value m__parent{};
    ::cw::lib::Value m_defaultvalue{};
    std::vector<::cw::lib::String> m_initialization{};
    bool m_is_writable{};
    ::cw::lib::Value m_name{};
    ::cw::lib::Value m_type{};
    ::cw::lib::Value m_value{};
    void initialize(std::optional<::cw::lib::Value> arg_parent, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_data, ::cw::lib::Value arg_value, ::cw::lib::String arg_name, ::cw::lib::Value arg_defaultvalue);
    void set(::cw::lib::Value arg_value, bool arg_updatedebugger);
    // PythonのVariantの値として厳密に等価比較し、表示用の文字列では比較しない。
    static bool values_equal(const ::cw::lib::Value& lhs, const ::cw::lib::Value& rhs);
    static ::cw::lib::String value_to_type(::cw::lib::Value arg_value);
    static ::cw::lib::Value value_from_element(std::shared_ptr<::cw::data::CWPyElement> arg_e, ::cw::lib::String arg_typeattr, std::optional<::cw::lib::String> arg_valueattr);
    static ::cw::lib::String value_to_str(::cw::lib::Value arg_value, bool arg_splitlines);
    static void value_to_element(::cw::lib::Value arg_value, std::shared_ptr<::cw::data::CWPyElement> arg_e, ::cw::lib::String arg_typeattr);
    ::cw::lib::String string_value();
    void write_value();
    static void _write_value(std::shared_ptr<::cw::data::CWPyElement> arg_e, ::cw::lib::Value arg_val);
};

class YadoData : public ::cw::lib::Object {
public:
    YadoData() = default;
    virtual ~YadoData() = default;
    inline static ::cw::lib::Value s__VarsKey{};
    inline static std::vector<std::shared_ptr<::cw::header::AdventurerHeader>> s_album{};
    inline static std::vector<std::tuple<std::vector<::cw::lib::String>, ::cw::lib::String>> s_bookmarks{};
    inline static std::vector<std::shared_ptr<::cw::image::ImageInfo>> s_imgpaths{};
    inline static ::cw::lib::Integer s_money{};
    inline static ::cw::lib::String s_name{};
    inline static std::vector<std::shared_ptr<::cw::header::PartyRecordHeader>> s_partyrecord{};
    inline static std::vector<std::shared_ptr<::cw::header::PartyHeader>> s_partys{};
    inline static std::shared_ptr<::cw::setting::RecentHistory> s_recenthistory{};
    inline static std::map<std::tuple<::cw::lib::String, ::cw::lib::String>, std::tuple<std::shared_ptr<::cw::data::CWPyElement>, std::map<::cw::lib::String, bool>, std::map<::cw::lib::String, ::cw::lib::Integer>, std::map<::cw::lib::String, ::cw::lib::Value>>> s_saved_variables{};
    inline static std::map<std::tuple<::cw::lib::String, ::cw::lib::String>, std::shared_ptr<::cw::header::SavedJPDCImageHeader>> s_savedjpdcimage{};
    inline static ::cw::lib::String s_skindirname{};
    inline static std::vector<std::shared_ptr<::cw::header::CardHeader>> s_storehouse{};
    bool m__changed{};
    bool m__loading{};
    std::vector<std::shared_ptr<::cw::header::AdventurerHeader>> m_album{};
    // PythonのList[Tuple[List[str], str]]を動的Valueへ潰さず保持する。
    std::vector<std::tuple<std::vector<::cw::lib::String>, ::cw::lib::String>> m_bookmarks{};
    std::shared_ptr<::cw::data::YadoDeletedPathSet> m_deletedpaths{};
    std::shared_ptr<::cw::data::CWPyElementTree> m_environment{};
    std::vector<std::shared_ptr<::cw::image::ImageInfo>> m_imgpaths{};
    ::cw::lib::String m_lastparty{};
    std::optional<std::shared_ptr<::cw::data::ScenarioData>> m_losted_sdata{};
    ::cw::lib::Integer m_money{};
    ::cw::lib::String m_name{};
    std::optional<std::shared_ptr<::cw::data::Party>> m_party{};
    std::vector<std::shared_ptr<::cw::header::PartyRecordHeader>> m_partyrecord{};
    std::vector<std::shared_ptr<::cw::header::PartyHeader>> m_partys{};
    std::shared_ptr<::cw::setting::RecentHistory> m_recenthistory{};
    std::map<std::tuple<::cw::lib::String, ::cw::lib::String>, std::tuple<std::shared_ptr<::cw::data::CWPyElement>, std::map<::cw::lib::String, bool>, std::map<::cw::lib::String, ::cw::lib::Integer>, std::map<::cw::lib::String, ::cw::lib::Value>>> m_saved_variables{};
    std::map<std::tuple<::cw::lib::String, ::cw::lib::String>, std::shared_ptr<::cw::header::SavedJPDCImageHeader>> m_savedjpdcimage{};
    ::cw::lib::String m_skindirname{};
    std::vector<std::shared_ptr<::cw::header::AdventurerHeader>> m_standbys{};
    std::vector<std::shared_ptr<::cw::header::CardHeader>> m_storehouse{};
    ::cw::lib::String m_tempdir{};
    std::shared_ptr<::cw::yadodb::YadoDB> m_yadodb{};
    ::cw::lib::String m_yadodir{};
    void initialize(::cw::lib::String arg_yadodir, ::cw::lib::String arg_tempdir, bool arg_loadparty);
    void update_version();
    void changed();
    bool is_changed();
    bool is_empty();
    bool is_loading();
    void set_skinname(::cw::lib::String arg_skindirname, ::cw::lib::String arg_skintype);
    ::cw::lib::String get_showingname();
    void load_party(std::optional<std::shared_ptr<::cw::header::PartyHeader>> arg_header);
    // 動的なスキンコマンド引数の接続用。型付きの呼び出しも同じ本体を使う。
    void load_party_value(::cw::lib::Value arg_header);
    // 解散前に検証し、持ち物の移管が成功してからキューへ追加する。
    std::vector<::cw::lib::String> prepare_party_removal(const std::shared_ptr<Party>& party) const;
    std::shared_ptr<::cw::header::AdventurerHeader> add_standbys(::cw::lib::String arg_path, bool arg_sort);
    std::shared_ptr<::cw::header::AdventurerHeader> add_album(::cw::lib::String arg_path);
    std::shared_ptr<::cw::header::PartyHeader> add_party(std::shared_ptr<::cw::data::Party> arg_party, bool arg_sort);
    std::shared_ptr<::cw::header::PartyRecordHeader> add_partyrecord(std::shared_ptr<::cw::thread::StoredParty> arg_partyrecord);
    std::shared_ptr<::cw::header::PartyRecordHeader> replace_partyrecord(std::shared_ptr<::cw::thread::StoredParty> arg_partyrecord);
    std::shared_ptr<::cw::header::PartyRecordHeader> set_partyrecord(::cw::lib::Integer arg_index, std::shared_ptr<::cw::thread::StoredParty> arg_partyrecord);
    void remove_partyrecord(std::shared_ptr<::cw::header::PartyRecordHeader> arg_header);
    void remove_emptypartyrecord();
    bool can_restoreparty(std::shared_ptr<::cw::header::PartyRecordHeader> arg_partyrecordheader);
    bool can_restore(::cw::lib::String arg_member);
    std::vector<std::shared_ptr<::cw::header::AdventurerHeader>> get_restoremembers(std::shared_ptr<::cw::header::PartyRecordHeader> arg_partyrecordheader);
    std::vector<std::shared_ptr<::cw::header::AdventurerHeader>> restore_party(std::shared_ptr<::cw::header::PartyRecordHeader> arg_partyrecordheader);
    // 記録の再生手順は一つにする。コールバックは通常のゲーム内の
    // 受け渡し処理へ接続し、 別の持ち物管理やUIを実装しない。
    void restore_partyrecord_backpack(const std::shared_ptr<CWPyElement>& record,
        const std::shared_ptr<Party>& party,
        const std::function<void(std::shared_ptr<::cw::header::CardHeader>)>& transfer);
    std::shared_ptr<::cw::header::AdventurerHeader> create_advheader(::cw::lib::String arg_path, bool arg_album, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_element);
    std::shared_ptr<::cw::header::CardHeader> create_cardheader(::cw::lib::String arg_path, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_element, std::optional<std::shared_ptr<::cw::character::Character>> arg_owner);
    std::shared_ptr<::cw::header::PartyHeader> create_partyheader(::cw::lib::String arg_path, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_element);
    void create_party(std::shared_ptr<::cw::header::AdventurerHeader> arg_header, bool arg_chgarea);
    void sort_standbys();
    void sort_parties();
    void sort_storehouse(std::optional<std::shared_ptr<::cw::character::Character>> arg_test_aptitude);
    void sort_partyrecord();
    void save();
    void _retry_save();
    void _transfer_temp();
    std::set<::cw::lib::String> get_gossips();
    std::set<::cw::lib::String> get_compstamps();
    std::vector<::cw::lib::String> get_gossiplist();
    std::vector<::cw::lib::String> get_compstamplist();
    bool has_compstamp(::cw::lib::String arg_name);
    bool has_gossip(::cw::lib::String arg_name);
    void set_compstamp(::cw::lib::String arg_name);
    void set_gossip(::cw::lib::String arg_name);
    void remove_compstamp(::cw::lib::String arg_name);
    void remove_gossip(::cw::lib::String arg_name);
    void clear_compstamps();
    void clear_gossips();
    ::cw::lib::Integer find_gossip(::cw::lib::Callback arg_matcher, ::cw::lib::Integer arg_startindex);
    ::cw::lib::String get_gossip_at(::cw::lib::Integer arg_index);
    ::cw::lib::Integer gossips_len();
    void set_money(::cw::lib::Integer arg_value, bool arg_blink);
    void join_npcs();
    std::set<::cw::lib::String> get_nowplayingpaths();
    std::vector<::cw::lib::String> get_partypaths();
    std::vector<::cw::lib::String> get_storehousepaths();
    std::vector<::cw::lib::String> get_standbypaths();
    std::vector<::cw::lib::String> get_albumpaths();
    void add_bookmark(std::vector<::cw::lib::String> arg_spaths, ::cw::lib::String arg_path);
    void set_bookmarks(std::vector<std::tuple<std::vector<::cw::lib::String>, ::cw::lib::String>> arg_bookmarks);
    static std::map<std::tuple<::cw::lib::String, ::cw::lib::String>, std::tuple<std::shared_ptr<::cw::data::CWPyElement>, std::map<::cw::lib::String, bool>, std::map<::cw::lib::String, ::cw::lib::Integer>, std::map<::cw::lib::String, ::cw::lib::Value>>> get_savedvariables(std::shared_ptr<::cw::data::CWPyElementTree> arg_environment);
    static std::tuple<std::map<::cw::lib::String, bool>, std::map<::cw::lib::String, ::cw::lib::Integer>, std::map<::cw::lib::String, ::cw::lib::Value>> _get_vartables(std::shared_ptr<::cw::data::CWPyElement> arg_e);
    static std::map<::cw::lib::String, std::tuple<std::shared_ptr<::cw::data::CWPyElement>, std::map<::cw::lib::String, bool>, std::map<::cw::lib::String, ::cw::lib::Integer>, std::map<::cw::lib::String, ::cw::lib::Value>>> get_savedvariables_with_skin(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    static std::map<std::tuple<::cw::lib::String, ::cw::lib::String>, std::tuple<std::shared_ptr<::cw::data::CWPyElement>, std::map<::cw::lib::String, bool>, std::map<::cw::lib::String, ::cw::lib::Integer>, std::map<::cw::lib::String, ::cw::lib::Value>>> get_savedvariables_with_scenario(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    void save_variables(::cw::lib::String arg_scenario, ::cw::lib::String arg_author, bool arg_complete, std::map<::cw::lib::String, std::shared_ptr<::cw::data::Flag>> arg_flags, std::map<::cw::lib::String, std::shared_ptr<::cw::data::Step>> arg_steps, std::map<::cw::lib::String, std::shared_ptr<::cw::data::Variant>> arg_variants, std::optional<std::shared_ptr<::cw::debug::logging::DebugLog>> arg_debuglog);
    void remove_savedvariables(::cw::lib::String arg_scenario, ::cw::lib::String arg_author);
};

class YadoDeletedPathSet : public ::cw::lib::Object {
public:
    YadoDeletedPathSet() = default;
    virtual ~YadoDeletedPathSet() = default;
    std::set<::cw::lib::String> m__s{};
    ::cw::lib::String m_tempdir{};
    ::cw::lib::String m_yadodir{};
    void initialize(::cw::lib::String arg_yadodir, ::cw::lib::String arg_tempdir);
    void write_list();
    bool read_list();
    std::vector<::cw::lib::String> iterate();
    bool contains(::cw::lib::String arg_path);
    void add(::cw::lib::String arg_path, bool arg_forceyado = false);
    void remove(::cw::lib::String arg_path);
    void discard(::cw::lib::String arg_path);
    void update(std::vector<::cw::lib::String> arg_s);
    void clear();
};

class _CWPyElementInterface : public ::cw::lib::Object {
public:
    _CWPyElementInterface() = default;
    virtual ~_CWPyElementInterface() = default;
    inline static ::cw::lib::String s_fpath{};
    [[noreturn]] void _raiseerror(::cw::lib::String arg_path, ::cw::lib::String arg_attr);
    virtual std::optional<std::shared_ptr<::cw::data::CWPyElement>> find(::cw::lib::String arg_match);
    bool hasfind(::cw::lib::String arg_path, ::cw::lib::String arg_attr);
    ::cw::lib::Value getfind(::cw::lib::String arg_path, bool arg_raiseerror);
    ::cw::lib::String gettext(::cw::lib::String arg_path, std::optional<::cw::lib::String> arg_py_default);
    ::cw::lib::String getattr(::cw::lib::String arg_path, ::cw::lib::String arg_attr, std::optional<::cw::lib::String> arg_py_default);
    bool getbool(::cw::lib::String arg_path, std::optional<::cw::lib::Value> arg_attr, std::optional<bool> arg_py_default);
    ::cw::lib::Integer getint(::cw::lib::String arg_path, std::optional<::cw::lib::Value> arg_attr, std::optional<::cw::lib::Integer> arg_py_default);
    ::cw::lib::Real getfloat(::cw::lib::String arg_path, std::optional<::cw::lib::Value> arg_attr, std::optional<::cw::lib::Real> arg_py_default);
    std::shared_ptr<::cw::data::CWPyElement> make_element(
        ::cw::lib::String arg_name, ::cw::lib::String arg_text = {},
        std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_attrs = std::nullopt,
        ::cw::lib::String arg_tail = {});
};

class _CWPyElementIterator : public ::cw::lib::Object {
public:
    _CWPyElementIterator() = default;
    virtual ~_CWPyElementIterator() = default;
    ::cw::lib::Integer m__iter{};
    std::shared_ptr<::cw::data::CWPyElement> m_e{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_e);
    std::vector<std::shared_ptr<::cw::data::CWPyElement>> iterate();
    std::shared_ptr<::cw::data::CWPyElement> next();
};

class _ElementWrapper : public ::cw::lib::Object {
public:
    _ElementWrapper() = default;
    virtual ~_ElementWrapper() = default;
    ::cw::lib::Value m_wrap{};
    ::cw::lib::String m_tag{};
    std::map<::cw::lib::String, ::cw::lib::String> m_attrib{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_wrap, ::cw::lib::String arg_tag, std::map<::cw::lib::String, ::cw::lib::String> arg_attrib);
};

// PyのElementの親子循環はGCで回収できるが、C++の接続中の親は弱
// 参照にする。 切り離したXMLのコピーは外側のコンテキストを所有する。両者
// をこのnull許容の親ハンドルで表し、 Objectやデバッガーからの参照
// も含め、接続済みの部分木が解放不能にならないようにする。 実行時の呼び出
// し元はツリー・イベントのコンテキストを保持する。 子だけを残してコンテキ
// ストを解放すると、PyのGC管理の循環とは異なり弱参照の親は失われる。
// 切り離したコピーは、再接続するまで外側のコンテキストを明示的に保持する。
class CWPyElementParent {
    std::weak_ptr<CWPyElement> link_;
    std::shared_ptr<CWPyElement> context_;
    std::vector<std::shared_ptr<CWPyElement>> ancestors_;
    void retain(std::shared_ptr<CWPyElement> parent);
public:
    CWPyElementParent() = default;
    CWPyElementParent(std::shared_ptr<CWPyElement> parent) { *this = std::move(parent); }
    CWPyElementParent(const CWPyElementParent& other) { *this = other; }
    CWPyElementParent(CWPyElementParent&&) = default;
    CWPyElementParent& operator=(CWPyElementParent&&) = default;
    CWPyElementParent& operator=(const CWPyElementParent& other) {
        if (this != &other) *this = *other;
        return *this;
    }
    CWPyElementParent& operator=(std::shared_ptr<CWPyElement> parent) {
        retain(std::move(parent)); return *this;
    }
    CWPyElementParent& operator=(std::nullopt_t) { reset(); return *this; }
    void attach(const std::shared_ptr<CWPyElement>& parent) { context_.reset(); ancestors_.clear(); link_ = parent; }
    void reset() { link_.reset(); context_.reset(); ancestors_.clear(); }
    bool has_value() const { return !link_.expired(); }
    explicit operator bool() const { return has_value(); }
    std::shared_ptr<CWPyElement> operator*() const { return link_.lock(); }
    std::shared_ptr<CWPyElement> value() const {
        auto parent = **this; if (!parent) throw std::bad_optional_access{}; return parent;
    }
    std::shared_ptr<CWPyElement> value_or(std::shared_ptr<CWPyElement> fallback) const {
        auto parent = **this; return parent ? parent : std::move(fallback);
    }
};

class CWPyElement : public ::cw::data::_CWPyElementInterface,
                    public std::enable_shared_from_this<CWPyElement> {
public:
    CWPyElement() = default;
    explicit CWPyElement(::cw::lib::String tag, ::cw::lib::String text = {},
                         ::cw::lib::XmlAttributes attributes = {},
                         ::cw::lib::String tail = {});
    virtual ~CWPyElement() = default;
    // ElementTree互換の具体的な保持領域。生成済みのラッパーフィール
    // ドはソース互換用に残し、 移植済みのモジュールはこれらのメンバーを使う。
    ::cw::lib::String m_tag{};
    ::cw::lib::String m_text{};
    ::cw::lib::String m_tail{};
    ::cw::lib::XmlAttributes m_attributes{};
    std::vector<std::shared_ptr<::cw::data::CWPyElement>> m_children{};
    std::weak_ptr<::cw::data::CWPyElement> m_parent{};
    std::optional<::cw::lib::Integer> m__cwxline_index{};
    ::cw::lib::Integer m__iter{};
    std::optional<std::shared_ptr<::cw::content::EventContentBase>> m_content{};
    ::cw::data::CWPyElementParent m_cwxparent{};
    std::optional<::cw::lib::String> m_cwxpath{};
    std::shared_ptr<::cw::data::_ElementWrapper> m_element{};
    ::cw::lib::String m_fpath{};
    std::optional<bool> m_needcheck{};
    std::optional<std::vector<std::shared_ptr<::cw::data::CWPyElement>>> m_nextelements{};
    void initialize(::cw::lib::String arg_tag, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_attrib);
    ::cw::lib::String get_tag();
    void set_tag(::cw::lib::String arg_tag);
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> find(::cw::lib::String arg_path) override;
    std::shared_ptr<::cw::data::CWPyElement> find_exists(::cw::lib::String arg_path);
    ::cw::lib::XmlAttributes get_attrib();
    ::cw::lib::String get_text();
    void set_text(::cw::lib::String arg_text);
    ::cw::lib::String get_tail();
    void set_tail(::cw::lib::String arg_tail);
    ::cw::lib::String get(::cw::lib::String arg_key, ::cw::lib::String arg_defaultvalue);
    void set(::cw::lib::String arg_key, ::cw::lib::String arg_value);
    ::cw::lib::Value keys();
    ::cw::lib::Value items();
    void append(std::shared_ptr<::cw::data::CWPyElement> arg_subelement);
    void extend(std::vector<std::shared_ptr<::cw::data::CWPyElement>> arg_subelements);
    void insert(::cw::lib::Integer arg_index, std::shared_ptr<::cw::data::CWPyElement> arg_subelement);
    void remove(std::shared_ptr<::cw::data::CWPyElement> arg_subelement);
    void clear();
    ::cw::lib::Integer index(std::shared_ptr<::cw::data::CWPyElement> arg_subelement, std::optional<::cw::lib::Integer> arg_start, std::optional<::cw::lib::Integer> arg_end);
    ::cw::lib::String get_cwxpath();
    std::vector<std::shared_ptr<::cw::data::CWPyElement>> iter(std::optional<::cw::lib::String> arg_tag);
    ::cw::lib::String representation();
    ::cw::lib::Integer size();
    bool to_bool();
    std::shared_ptr<::cw::data::CWPyElement> get_item(::cw::lib::Integer arg_index);
    std::vector<std::shared_ptr<::cw::data::CWPyElement>> get_item_2(::cw::lib::Value arg_index);
    ::cw::lib::Value get_item_3(::cw::lib::Value arg_index);
    void set_item(::cw::lib::Integer arg_index, std::shared_ptr<::cw::data::CWPyElement> arg_element);
    void delete_item(::cw::lib::Value arg_index);
    std::vector<std::shared_ptr<::cw::data::CWPyElement>> iterate();
    std::vector<std::shared_ptr<::cw::data::CWPyElement>> py_reversed();
};

class CWPyElementTree : public ::cw::data::_CWPyElementInterface {
public:
    CWPyElementTree() = default;
    explicit CWPyElementTree(std::shared_ptr<::cw::data::CWPyElement> root,
                             ::cw::lib::String fpath = {});
    virtual ~CWPyElementTree() = default;
    std::shared_ptr<::cw::data::CWPyElement> m_root_node{};
    ::cw::lib::Value m__etree{};
    ::cw::lib::Value m__root{};
    ::cw::lib::String m_fpath{};
    bool m_is_edited{};
    // 読み込み元がマウント済みUserDataのStorage内と明示的に解決された
    // 場合だけ設定する。 汎用の資産・シナリオXMLはネイティブの処理を維持する。
    bool m_user_storage_backed{};
    void initialize(::cw::lib::String arg_fpath, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_element);
    std::shared_ptr<::cw::data::CWPyElement> getroot();
    std::vector<std::shared_ptr<::cw::data::CWPyElement>> iter();
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> find(::cw::lib::String arg_path) override;
    std::shared_ptr<::cw::data::CWPyElement> find_exists(::cw::lib::String arg_path);
    ::cw::lib::Bytes write_bytes();
    void write_file(::cw::lib::String arg_path);
    void write_xml(bool arg_nocheck_edited);
    void edit(::cw::lib::String arg_path, ::cw::lib::String arg_value, std::optional<::cw::lib::String> arg_attrname);
    void append(::cw::lib::String arg_path, std::shared_ptr<::cw::data::CWPyElement> arg_element);
    void insert(::cw::lib::String arg_path, std::shared_ptr<::cw::data::CWPyElement> arg_element, ::cw::lib::Integer arg_index);
    void remove(::cw::lib::String arg_path, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_element, std::optional<::cw::lib::String> arg_attrname);
    void form_element(std::shared_ptr<::cw::data::CWPyElement> arg_element, ::cw::lib::Integer arg_depth);
};

class ScenarioData : public ::cw::data::SystemData {
public:
    ScenarioData() = default;
    virtual ~ScenarioData() = default;
    ::cw::lib::String m__arcname{};
    bool m__cancel_decompress{};
    ::cw::lib::String m_extracted_archive_root{};
    ::cw::lib::ValueSet m__datafilenames{};
    ::cw::lib::Value m__error{};
    // PythonのScenarioDataは下のSystemDataの
    // 属性を共用する。C++でも保持先を SystemDataだけにし、
    // 基底・派生の呼び出しで異なるシナリオ状態を参照しないようにする。
    ::cw::lib::String m__format{};
    bool m__progress{};
    std::optional<::cw::lib::DateTime> m__start_datetime{};
    // SystemData::m_backlogを継承して使う。PythonのScenarioData
    // も同じ属性へ代入する。 再宣言するとメッセージの書き込みとステー
    // タスバー・F5の読み込みが分かれ、ログのボタンが有効にならない。
    void initialize(std::shared_ptr<::cw::header::ScenarioHeader> arg_header, bool arg_cardonly);
    void _init_ignorecase_table();
    void check_archiveupdated(bool arg_reload);
    ::cw::lib::String _decompress(bool arg_overwrite);
    void _find_summaryintemp();
    std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>> get_versionhint(::cw::lib::Integer arg_frompos);
    void set_versionhint(::cw::lib::Integer arg_pos, std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>> arg_hint);
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> get_carddata(std::shared_ptr<::cw::data::CWPyElement> arg_linkdata, bool arg_in_inusecard, std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_inusecardheader);
    void save_breakpoints();
    std::optional<::cw::lib::DateTime> get_startdatetime();
    ::cw::lib::Real get_totalpyaingtime();
    ::cw::lib::Real get_pausedtime();
    void start_timekeeper();
    void resume_timekeeper();
    void sleep_timekeeper();
    bool change_data(::cw::lib::Integer arg_resid, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_data);
    void reload();
    void save_variables();
    void load_variables();
    void update_skin();
    void _reload();
    bool is_updatedfilenames();
    void _init_xmlpaths(bool arg_xmlonly);
    void update_scale();
    bool eat_spchar(::cw::lib::String arg_dpath, ::cw::lib::String arg_fname, bool arg_can_loaded_scaledimage);
    void start();
    void end(bool arg_showdebuglog, bool arg_complete, bool arg_failure);
    void f9();
    void create_log();
    std::vector<std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, bool, ::cw::lib::String>> load_log(::cw::lib::String arg_path, bool arg_recording);
    void update_log();
    std::vector<::cw::lib::String> get_bgmpaths();
    void fullrecovery_fcards();
};

extern ::cw::lib::Value g_VariantValueType;
extern std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> g__WSN_DATA_DIRS;
extern ::cw::lib::Value g__lock;

std::vector<::cw::lib::String> parse_initialization(std::optional<::cw::lib::Value> arg_value);
void SystemData_update_skin_func();
void SystemData_update_scenariopath_update_table(std::map<::cw::lib::Integer, std::tuple<::cw::lib::String, ::cw::lib::String>> arg_table);
std::tuple<std::set<::cw::lib::String>, std::map<::cw::lib::String, std::tuple<::cw::lib::String, ::cw::lib::String>>> get_skinkeys();
void ScenarioData__decompress_startup(::cw::lib::Integer arg_filenum);
void ScenarioData__decompress_startup_func();
bool ScenarioData__decompress_progress(::cw::lib::Integer arg_cur);
void ScenarioData__decompress_progress_func();
void ScenarioData__decompress_run_decompress();
::cw::lib::DateTime ScenarioData_resume_timekeeper_parse_datetime(::cw::lib::String arg_tag);
void ScenarioData_update_skin_func();
void ScenarioData__reload_func();
std::tuple<std::shared_ptr<::cw::lib::Surface>, bool> ScenarioData_eat_spchar_load(::cw::lib::String arg_dpath, ::cw::lib::String arg_fname);
std::map<::cw::lib::String, std::shared_ptr<::cw::data::Flag>> init_flags(::cw::lib::Value arg_data, bool arg_writable);
std::map<::cw::lib::String, std::shared_ptr<::cw::data::Step>> init_steps(::cw::lib::Value arg_data, bool arg_writable);
std::map<::cw::lib::String, std::shared_ptr<::cw::data::Variant>> init_variants(::cw::lib::Value arg_data, bool arg_writable);
void redraw_cards(bool arg_value, ::cw::lib::String arg_flag, bool arg_silent);
::cw::lib::String Variant_value_to_str_to_str(::cw::lib::Value arg_val);
::cw::lib::Value Variant_value_to_str_lambda_line_2636(::cw::lib::Value arg_s);
void YadoData_save_commit_timekeeper(::cw::lib::String arg_ppath, bool arg_is_adventuring);
void YadoData_save_update_backpack(std::shared_ptr<::cw::data::Party> arg_party);
void YadoData_save_update_database(::cw::lib::String arg_yadodir);
::cw::lib::String find_scefullpath(::cw::lib::String arg_scepath, std::vector<::cw::lib::String> arg_spaths);
void sort_cards(std::vector<std::shared_ptr<::cw::header::CardHeader>>& arg_cards, ::cw::lib::String arg_condition, bool arg_withstar);
void sort_cards_addetckey();
::cw::lib::Value CWPyElement_extend_lambda_line_4928(::cw::lib::Value arg_e);
std::shared_ptr<::cw::data::CWPyElement> make_element(
    ::cw::lib::String arg_name, ::cw::lib::String arg_text = {},
    std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_attrs = std::nullopt,
    ::cw::lib::String arg_tail = {});
std::shared_ptr<::cw::data::CWPyElementTree> yadoxml2etree(::cw::lib::String arg_path, ::cw::lib::String arg_tag, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs, ::cw::lib::String saved_root={}, ::cw::lib::String temp_root={});
std::shared_ptr<::cw::data::CWPyElementTree> yadoxml2etree(::cw::lib::String arg_path, ::cw::lib::String arg_tag, std::map<::cw::lib::String, ::cw::lib::String>& arg_rootattrs, ::cw::lib::String saved_root={}, ::cw::lib::String temp_root={});
std::shared_ptr<::cw::data::CWPyElement> yadoxml2element(::cw::lib::String arg_path, ::cw::lib::String arg_tag, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs);
std::shared_ptr<::cw::data::CWPyElement> yadoxml2element(::cw::lib::String arg_path, ::cw::lib::String arg_tag, std::map<::cw::lib::String, ::cw::lib::String>& arg_rootattrs);
// 相対パスはすべてbase_directory（既定はEngineの作業ディレクトリ）基準で解決
// する。 ./からのUserData/Yadoと./UserDataからのYadoは同じパスであ
// り、形式の違いではない。 未確定のTempデータを宿の保存済みファイルより優先する。 Pyのyadoxml2element
// と同様に、無関係のパスは拒否し、ファイルがなければ例外を投げる。
std::shared_ptr<::cw::data::CWPyElement> read_yadoxml(
    ::cw::lib::String path, ::cw::lib::String yado_root, ::cw::lib::String temp_root,
    ::cw::lib::String tag = {},
    std::optional<std::map<::cw::lib::String, ::cw::lib::String>> rootattrs = std::nullopt,
    ::cw::lib::String base_directory = ".");
std::shared_ptr<::cw::data::CWPyElement> read_yadoxml(
    ::cw::lib::String path, ::cw::lib::String yado_root, ::cw::lib::String temp_root,
    ::cw::lib::String tag,
    std::map<::cw::lib::String, ::cw::lib::String>& rootattrs,
    ::cw::lib::String base_directory = ".");
std::shared_ptr<::cw::data::CWPyElementTree> xml2etree(::cw::lib::String arg_path, ::cw::lib::String arg_tag, std::optional<::cw::lib::Value> arg_stream, std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_element, bool arg_nocache);
std::shared_ptr<::cw::data::CWPyElement> xml2element(::cw::lib::String arg_path, ::cw::lib::String arg_tag, std::optional<::cw::lib::Value> arg_stream, std::optional<::cw::lib::String> arg_xmltext, bool arg_nocache, std::optional<std::map<::cw::lib::String, ::cw::lib::String>> arg_rootattrs);
std::shared_ptr<::cw::data::CWPyElement> xml2element(::cw::lib::String arg_path, ::cw::lib::String arg_tag, std::optional<::cw::lib::Value> arg_stream, std::optional<::cw::lib::String> arg_xmltext, bool arg_nocache, std::map<::cw::lib::String, ::cw::lib::String>& arg_rootattrs);
std::shared_ptr<::cw::data::CWPyElement> copydata(std::shared_ptr<::cw::data::CWPyElement> arg_data, bool arg_copyall);
std::shared_ptr<::cw::data::CWPyElementTree> copytree(std::shared_ptr<::cw::data::CWPyElementTree> arg_data, bool arg_copyall);
void main();
}
