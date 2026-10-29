#pragma once

// cw/event.pyのC++20移植。Pythonの動的オブジェクトに対応
// する状態を型付きで保持し、 イベントスタック・対象・変数の遷移を明示する。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"

namespace cw::event {
class Event : public ::cw::lib::Object, public std::enable_shared_from_this<Event> {
public:
    Event() = default;
    virtual ~Event() = default;
    inline static ::cw::lib::String s_author{};
    inline static std::optional<std::shared_ptr<::cw::data::CWPyElement>> s_force_nextcontent{};
    inline static ::cw::lib::Integer s_force_nextcontent_index{};
    inline static bool s_is_active{};
    inline static ::cw::lib::String s_keycode_matching{};
    inline static ::cw::lib::Integer s_line_index{};
    inline static ::cw::lib::Integer s_packageid{};
    inline static ::cw::lib::String s_scenario{};
    inline static bool s_skip_action{};
    ::cw::lib::Value m__event{};
    bool m__reset_changestate{};
    std::vector<bool> m__stored_in_cardeffectmotion{};
    std::vector<bool> m__stored_in_inusecardevent{};
    std::vector<::cw::lib::Value> m__stored_specialchars{};
    std::optional<std::tuple<::cw::lib::Integer, std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>>>> m__versionhint_base{};
    ::cw::lib::String m_author{};
    std::optional<std::shared_ptr<::cw::event::Event>> m_base{};
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> m_cur_content{};
    std::optional<std::shared_ptr<::cw::event::EventError>> m_error{};
    std::map<::cw::lib::String, std::shared_ptr<::cw::data::Flag>> m_flags{};
    ::cw::lib::Value m_force_nextcontent{};
    ::cw::lib::Value m_force_nextcontent_index{};
    ::cw::lib::Integer m_index{};
    std::optional<std::shared_ptr<::cw::header::CardHeader>> m_inusecard{};
    bool m_is_active{};
    ::cw::lib::String m_keycode_matching{};
    std::vector<::cw::lib::String> m_keycodes{};
    std::vector<::cw::lib::Integer> m_keynums{};
    ::cw::lib::Integer m_line_index{};
    std::vector<std::tuple<std::optional<std::shared_ptr<::cw::event::Event>>, std::shared_ptr<::cw::data::CWPyElement>, ::cw::lib::Integer, std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>>>> m_nowrunningcontents{};
    ::cw::lib::Integer m_packageid{};
    // 実行中のスタックが親を所有するため、逆参照で所有権の循環を作らない。
    std::weak_ptr<::cw::event::Event> m_parent{};
    ::cw::lib::String m_scenario{};
    bool m_skip_action{};
    std::optional<std::shared_ptr<::cw::data::CWPyElement>> m_starttree{};
    std::map<::cw::lib::String, std::shared_ptr<::cw::data::Step>> m_steps{};
    std::vector<::cw::lib::String> m_treekeys{};
    std::map<::cw::lib::String, std::shared_ptr<::cw::data::CWPyElement>> m_trees{};
    std::map<::cw::lib::String, std::shared_ptr<::cw::data::Variant>> m_variants{};
    void set_variant_binding(const ::cw::lib::String& name, std::shared_ptr<::cw::data::Variant> variant);
    void initialize(std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_event);
    std::shared_ptr<::cw::event::Event> copy_event();
    void copy_from(std::shared_ptr<::cw::event::Event> arg_event, std::optional<std::tuple<::cw::lib::Integer, std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>>>> arg_versionhint_base);
    void copy_from_impl(std::shared_ptr<::cw::event::Event> arg_event);
    void store_inusedata(bool arg_selectuser);
    void restore_inusedata();
    void start(bool arg_skip_effect_card_motion);
    void stop();
    void run(bool arg_perf, bool arg_isinside, bool arg_skip_effect_card_motion);
    void run_exit();
    void run_exit_inside();
    void end();
    std::optional<std::shared_ptr<::cw::event::EventEngine>> get_events(std::shared_ptr<::cw::sprite::card::CWPyCard> arg_target);
    std::optional<std::shared_ptr<::cw::event::Event>> ignition_characterevent(std::shared_ptr<::cw::character::Character> arg_target, bool arg_can_unconscious, std::vector<::cw::lib::String> arg_keycodes);
    std::optional<std::shared_ptr<::cw::event::Event>> ignition_deadevent(std::shared_ptr<::cw::character::Character> arg_target, std::vector<::cw::lib::String> arg_keycodes);
    std::optional<std::shared_ptr<::cw::event::Event>> ignition_menucardevent(std::shared_ptr<::cw::sprite::card::MenuCard> arg_target, std::vector<::cw::lib::String> arg_keycodes);
    std::vector<::cw::lib::String> _keycodes_for_successevent(std::vector<::cw::lib::String> arg_keycodes, bool arg_successflag);
    std::optional<std::shared_ptr<::cw::event::Event>> ignition_successevent(std::shared_ptr<::cw::character::Character> arg_target, bool arg_successflag, std::vector<::cw::lib::String> arg_keycodes);
    void run_scenarioevent(bool arg_isinsideevent);
    void clear();
    void action();
    std::optional<std::vector<std::shared_ptr<::cw::data::CWPyElement>>> get_nextcontents();
    void check_gameover();
};

class EventEngine : public ::cw::lib::Object {
public:
    EventEngine() = default;
    virtual ~EventEngine() = default;
    ::cw::lib::Value m__engine_role{};
    ::cw::lib::Value m_events{};
    // SetActionEventなどでは、親のないEvent要素からEventEngine
    // を作る場合がある。 Pythonと同等のEventsコンテナ
    // ーを保持し、cwxpathやデバッガーの走査が 切り離され
    // たEventルートを参照してゲームを中断しないようにする。
    std::shared_ptr<::cw::data::CWPyElement> m_synthetic_events_root{};
    // Python の動的 events リストを実行順序を保った型付きミラーで保持する。
    std::vector<std::shared_ptr<::cw::event::Event>> m_event_list{};
    ::cw::lib::String m_role{};
    void initialize(std::vector<std::shared_ptr<::cw::data::CWPyElement>> arg_data, ::cw::lib::String arg_role);
    bool start(std::optional<::cw::lib::Integer> arg_keynum, std::optional<std::vector<::cw::lib::String>> arg_keycodes, bool arg_isinsideevent, bool arg_successevent);
    std::optional<std::shared_ptr<::cw::event::Event>> check_keycodes(std::vector<::cw::lib::String> arg_keycodes, bool arg_successevent);
    bool check_keycode(::cw::lib::String arg_keycode, std::set<::cw::lib::String> arg_keycodes, bool arg_successevent);
    std::optional<std::shared_ptr<::cw::event::Event>> check_keynum(::cw::lib::Integer arg_keynum);
};

class EventError : public ::cw::lib::Object {
public:
    EventError() = default;
    EventError(const EventError&) noexcept = default;
    EventError& operator=(const EventError&) noexcept = default;
    virtual ~EventError() = default;
    // ネイティブの値による例外送出でも、保持しているPythonの例外の派生型を保つ。
    [[noreturn]] virtual void rethrow() const { throw *this; }
};

class EventInterface : public ::cw::lib::Object {
public:
    EventInterface() = default;
    virtual ~EventInterface() = default;
    ::cw::lib::Value m__eventinterfalerole{};
    std::optional<std::shared_ptr<::cw::header::CardHeader>> m__inusecard{};
    std::vector<std::shared_ptr<::cw::event::Event>> m__nowrunningevents{};
    std::optional<std::shared_ptr<::cw::header::CardHeader>> m__selectedcard{};
    std::optional<std::shared_ptr<::cw::character::Character>> m__selectedmember{};
    bool m__step{};
    ::cw::lib::Value m__targetstack{};
    bool m_breakwait{};
    std::optional<std::shared_ptr<::cw::event::Targeting>> m_effectevent{};
    ::cw::lib::Integer m_eventtimer{};
    std::optional<::cw::lib::Callback> m_exit_func{};
    bool m_in_cardeffectmotion{};
    bool m_in_inusecardevent{};
    bool m_is_changestate{};
    std::map<::cw::lib::Integer, std::tuple<std::shared_ptr<::cw::data::CWPyElement>, std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>>> m_nowrunningpacks{};
    bool m_paused{};
    std::vector<std::optional<::cw::lib::Value>> m_stackinfo{};
    ::cw::lib::Integer m_stackinfo_len{};
    bool m_stoped{};
    ::cw::lib::Integer m_waittime{};
    void initialize(::cw::lib::String arg_role);
    ::cw::lib::String get_selectedmembername();
    ::cw::lib::String get_selectedcardname();
    std::optional<::cw::lib::String> get_selectedcardtype();
    std::shared_ptr<::cw::event::Event> pop_event();
    void remove_event(std::shared_ptr<::cw::event::Event> arg_event);
    void append_event(std::shared_ptr<::cw::event::Event> arg_event);
    void replace_event(std::shared_ptr<::cw::event::Event> arg_event, std::optional<std::tuple<::cw::lib::Integer, std::optional<std::tuple<::cw::lib::String, ::cw::lib::String, bool, bool, bool>>>> arg_versionhint_base);
    void clear_events();
    std::optional<std::shared_ptr<::cw::event::Event>> get_event();
    std::optional<std::shared_ptr<::cw::event::Targeting>> get_effectevent();
    std::vector<std::shared_ptr<::cw::event::Event>> get_events();
    ::cw::lib::Integer get_currentstack();
    std::optional<std::map<::cw::lib::String, std::shared_ptr<::cw::data::CWPyElement>>> get_trees();
    std::vector<::cw::lib::String> get_treekeys();
    std::optional<std::shared_ptr<::cw::event::Event>> get_nowrunningevent();
    ::cw::lib::Integer get_packageid();
    void clear();
    void set_inusecard(std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_header);
    void set_selectedmember(std::optional<std::shared_ptr<::cw::character::Character>> arg_ccard);
    void set_selectedcard(std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_header);
    std::vector<::cw::lib::Value> get_targetscope(::cw::lib::String arg_scope, bool arg_unreversed, bool arg_cards, ::cw::lib::String arg_coupon);
    ::cw::lib::Value get_targetmember(::cw::lib::String arg_targetm, bool arg_unreversed, ::cw::lib::String arg_coupon);
    std::optional<std::shared_ptr<::cw::character::Character>> get_randommember();
    bool has_selectedmember();
    std::optional<std::shared_ptr<::cw::character::Character>> get_selectedmember();
    void clear_selectedmember();
    std::optional<std::shared_ptr<::cw::character::Character>> get_unselectedmember();
    std::optional<std::shared_ptr<::cw::character::Character>> get_firstmember(::cw::lib::Value arg_mode);
    std::optional<std::shared_ptr<::cw::header::CardHeader>> get_inusecard();
    std::optional<std::shared_ptr<::cw::header::CardHeader>> get_selectedcard();
    void clear_selectedcard();
    void refresh_tools();
    void refresh_showpartytools();
    void refresh_variablelist();
    void refresh_variable(::cw::lib::Value arg_variable);
    void refresh_selectedmembername();
    void refresh_selectedcardname();
    void refresh_areaname();
    void refresh_activeitem();
    void append_stackinfo(::cw::lib::Value arg_item);
    void pop_stackinfo();
    void replace_stackinfo(::cw::lib::Integer arg_index, ::cw::lib::Value arg_item);
    void clear_stackinfo();
    void wait();
    void set_curcontent(std::shared_ptr<::cw::data::CWPyElement> arg_content, std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    void set_stepexec(bool arg_step);
    bool is_stepexec();
    void set_stoped(bool arg_stoped);
    bool is_stoped();
    bool is_paused();
};

class Targeting : public ::cw::lib::Object {
public:
    Targeting() = default;
    virtual ~Targeting() = default;
    ::cw::lib::Value m__setcardtarget{};
    ::cw::lib::Integer m__target_index{};
    bool m__target_updated{};
    std::set<std::shared_ptr<::cw::sprite::card::CWPyCard>> m_coupon_owners{};
    std::optional<std::shared_ptr<::cw::effectmotion::Effect>> m_eff{};
    std::optional<std::shared_ptr<::cw::event::EventError>> m_error{};
    std::set<std::shared_ptr<::cw::sprite::card::CWPyCard>> m_mcards{};
    std::vector<::cw::lib::Value> m_targets{};
    // Value だけでは Character/MenuCard の実体を安全に走査できないため、
    // Python の targets と同じ順序を型付きで保持する。
    std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>> m_target_cards{};
    ::cw::lib::Value m_user{};
    std::optional<std::shared_ptr<::cw::sprite::card::CWPyCard>> m_user_card{};
    bool m_waited{};
    void initialize(std::optional<::cw::lib::Value> arg_user, std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>> arg_targets, bool arg_setcardtarget);
    void targets_to_coupon();
    void clear_eventcoupons();
    bool in_effectmotionloop();
    void update_targets();
    std::optional<std::shared_ptr<::cw::sprite::card::CWPyCard>> get_nexttarget();
    void add_target(std::shared_ptr<::cw::character::Character> arg_ccard);
    void remove_target(std::shared_ptr<::cw::character::Character> arg_ccard);
};

class ActionBreakError : public ::cw::event::EventError {
public:
    ActionBreakError() = default;
    explicit ActionBreakError(::cw::lib::String message) : m_message(std::move(message)) {}
    virtual ~ActionBreakError() = default;
    // この型付きエラーをコピー・再送出する際も、Pythonの例外文
    // 字列を保つ。 ネイティブの呼び出し元はwhat()で参照する。
    ::cw::lib::String m_message{};
    const char* what() const noexcept { return m_message.c_str(); }
    [[noreturn]] void rethrow() const override { throw *this; }
};

class AreaChangeError : public ::cw::event::EventError {
public:
    AreaChangeError() = default;
    virtual ~AreaChangeError() = default;
    [[noreturn]] void rethrow() const override { throw *this; }
};

class CardEvent : public ::cw::event::Event, public ::cw::event::Targeting {
public:
    CardEvent() = default;
    virtual ~CardEvent() = default;
    // カード・エリアのイベントが別のエリアやシナリオへ移る前に、スナップショットを取る。
    bool m_no_consume_unmatched_menu_card{};
    bool is_unmatched_menu_card_use();
    inline static ::cw::lib::String s_USE_CARD_KEYCODE{};
    inline static std::shared_ptr<::cw::header::CardHeader> s_inusecard{};
    // Pythonでは一つの名前に属性は一つだけ。実行状態はEvent、対象選択の状
    // 態はTargetingが所有する。 ここでどちらも同名メンバーとして隠さない。
    ::cw::lib::ValueMap m__flags{};
    ::cw::lib::ValueMap m__steps{};
    ::cw::lib::ValueMap m__variants{};
    ::cw::lib::Value m_skip_card_end_event{};
    void initialize(std::optional<std::shared_ptr<::cw::data::CWPyElement>> arg_event, std::shared_ptr<::cw::header::CardHeader> arg_inusecard, ::cw::lib::Value arg_user, std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>> arg_targets, bool arg_skip_card_end_event);
    bool _is_normal_start_event(std::shared_ptr<::cw::data::CWPyElement> arg_event);
    void _copy_normal_start_event(std::shared_ptr<::cw::event::Event> arg_event);
    void _select_normal_start_event();
    void copy_variables_from(::cw::lib::Value arg_other);
    void set_variable_initialize(::cw::lib::String arg_initType);
    void start(bool arg_skip_effect_card_motion);
    void start_with_content(std::shared_ptr<::cw::data::CWPyElement> arg_content,
                            bool arg_skip_effect_card_motion);
    void start_selected(bool arg_skip_effect_card_motion);
    void run_exit(bool arg_skip_effect_card_motion);
    void end();
    void _exit_event();
    void run_areaevent();
    void run_characterevent(std::shared_ptr<::cw::character::Character> arg_target, bool arg_can_unconscious);
    bool _has_usecard_event_keycode(std::shared_ptr<::cw::header::CardHeader> arg_header);
    std::optional<std::shared_ptr<::cw::event::Event>> _check_usecard_event(std::shared_ptr<::cw::event::EventEngine> arg_events, std::vector<::cw::lib::String> arg_keycodes, bool arg_successevent);
    std::optional<std::shared_ptr<::cw::header::CardHeader>> _get_selected_usecard_for_keycodeevent(std::shared_ptr<::cw::character::Character> arg_target);
    void run_target_selctcardevent(std::shared_ptr<::cw::character::Character> arg_target);
    void run_ownedcardevent(std::shared_ptr<::cw::character::Character> arg_target);
    bool run_deadevent(std::shared_ptr<::cw::character::Character> arg_target);
    void run_menucardevent(std::shared_ptr<::cw::sprite::card::MenuCard> arg_target);
    void run_successevent(std::shared_ptr<::cw::character::Character> arg_target, bool arg_successflag);
    void run_usecard_successevent(std::shared_ptr<::cw::character::Character> arg_target, bool arg_successflag);
    void run_ownedcard_successevent(std::shared_ptr<::cw::character::Character> arg_target, bool arg_successflag);
    void effect_cardmotion();
};

class EffectBreakError : public ::cw::event::EventError {
public:
    EffectBreakError() = default;
    virtual ~EffectBreakError() = default;
    [[noreturn]] void rethrow() const override { throw *this; }
    ::cw::lib::Value m_consumecard{true};
    void initialize(bool arg_consumecard);
};

class ScenarioBadEndError : public ::cw::event::EventError {
public:
    ScenarioBadEndError() = default;
    virtual ~ScenarioBadEndError() = default;
    [[noreturn]] void rethrow() const override { throw *this; }
};

class ScenarioEndError : public ::cw::event::EventError {
public:
    ScenarioEndError() = default;
    virtual ~ScenarioEndError() = default;
    [[noreturn]] void rethrow() const override { throw *this; }
};

class XmlContentCriticalError : public ::cw::event::EventError {
public:
    XmlContentCriticalError() = default;
    explicit XmlContentCriticalError(::cw::lib::String arg_msg) : m_msg(std::move(arg_msg)) {}
    virtual ~XmlContentCriticalError() = default;
    [[noreturn]] void rethrow() const override { throw *this; }
    ::cw::lib::String m_msg{};
};

class StartBattleError : public ::cw::event::AreaChangeError {
public:
    StartBattleError() = default;
    virtual ~StartBattleError() = default;
    [[noreturn]] void rethrow() const override { throw *this; }
};

void EventInterface_refresh_activeitem_func();
void EventInterface_wait_func();
std::vector<::cw::lib::String> _get_targetinfo();
std::map<::cw::lib::String, std::shared_ptr<::cw::data::Flag>> CardEvent_copy_variables_from_clone_flags(std::map<::cw::lib::String, std::shared_ptr<::cw::data::Flag>> arg_src);
std::map<::cw::lib::String, std::shared_ptr<::cw::data::Step>> CardEvent_copy_variables_from_clone_steps(std::map<::cw::lib::String, std::shared_ptr<::cw::data::Step>> arg_src);
std::map<::cw::lib::String, std::shared_ptr<::cw::data::Variant>> CardEvent_copy_variables_from_clone_variants(std::map<::cw::lib::String, std::shared_ptr<::cw::data::Variant>> arg_src);
void CardEvent_end_save_var(::cw::lib::Value arg_v);
void CardEvent_effect_cardmotion_clear_params(::cw::lib::Value arg_target);
std::tuple<bool, bool> get_effecttargetstatus(::cw::lib::Value arg_target, std::shared_ptr<::cw::effectmotion::Effect> arg_eff);
std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>> initial_effect(std::shared_ptr<::cw::effectmotion::Effect> arg_eff, std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>> arg_targets, bool arg_allrange, ::cw::lib::String arg_path, ::cw::lib::Integer arg_volume, ::cw::lib::Integer arg_loopcount, ::cw::lib::Integer arg_channel, ::cw::lib::Integer arg_fade, std::optional<::cw::lib::Callback> arg_remove_target);
void main();
bool is_unmatched_menu_card_use(std::shared_ptr<::cw::header::CardHeader> header,
    const std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>>& targets);
}

