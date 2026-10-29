#pragma once

// CardWirthPyの実行時コンテントの宣言。 XML読み込み用
// の補助処理と、action()が使う実行中イベントの状態を分ける。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include <mutex>

namespace cw::content {
// 直接初期化したコンテントはXMLを所有する。XMLのキャッシュへ登録
// した後は逆参照を弱参照へ切り替える。 get_contentは所有
// 権付きの実行ハンドルを返し、呼び出し元が使う間はXMLを維持する。
class ContentElementReference {
    std::shared_ptr<::cw::data::CWPyElement> owned_;
    std::weak_ptr<::cw::data::CWPyElement> weak_;
public:
    ContentElementReference& operator=(std::shared_ptr<::cw::data::CWPyElement> data) {
        owned_ = std::move(data);
        weak_ = owned_;
        return *this;
    }
    std::shared_ptr<::cw::data::CWPyElement> lock() const { return weak_.lock(); }
    operator std::shared_ptr<::cw::data::CWPyElement>() const { return lock(); }
    explicit operator bool() const { return !weak_.expired(); }
    std::shared_ptr<::cw::data::CWPyElement> operator->() const;
    void weaken() { owned_.reset(); }
};

class EventContentBase : public ::cw::lib::Object {
public:
    EventContentBase() = default;
    virtual ~EventContentBase() = default;
    inline static bool s__init_values{};
    std::optional<::cw::lib::String> m__author{};
    bool m__init_values{};
    bool m__inusecard{};
    std::optional<::cw::lib::String> m__scenario{};
    std::map<::cw::lib::String, ::cw::lib::Integer> m_coupons{};
    ::cw::lib::Integer m_initvalue{};
    ::cw::lib::Value m_is_changestate{};
    // 上のValue形式の参照は生成済みの呼び出し元向けに残す。 実際のElementTree
    // とイベント状態は、これらの型付きフィールドが保持する。
    ContentElementReference m_data_typed{};
    bool m_is_changestate_typed{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data, bool arg_is_changestate);
    virtual ::cw::lib::Integer action();
    // 仮想actionを実行し、現在のコンテントについて量を制限
    // した診断を1件出力する。 Event::action()か
    // らこのラッパーを使い、個々の具象action()へ計測を追
    // 加せずに 最上位のコンテント操作を識別できるようにする。
    ::cw::lib::Integer action_with_log();
    virtual bool can_action();
    virtual ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    virtual ::cw::lib::String get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event);
    ::cw::lib::String get_contentname(std::shared_ptr<::cw::data::CWPyElement> arg_child, ::cw::lib::String arg_py_default);
    std::vector<std::shared_ptr<::cw::data::CWPyElement>> get_children();
    ::cw::lib::Integer get_children_num();
    static std::tuple<::cw::lib::String, ::cw::lib::Value> get_transitiontype_static(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    std::tuple<::cw::lib::String, ::cw::lib::Value> get_transitiontype();
    void init_values();
    std::optional<std::shared_ptr<::cw::sprite::card::PlayerCard>> get_valuedmember(::cw::lib::Value arg_mode, bool arg_silenced_member);
    bool is_differentscenario(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    void set_result_variable(::cw::lib::String arg_name, ::cw::lib::Value arg_value);
    std::optional<std::shared_ptr<::cw::character::Character>> get_variable_target(::cw::lib::String arg_name);
    void update_bg_after();
    void variant_error(::cw::lib::String arg_msg, const ::cw::calculator::ComputeException* arg_ex, ::cw::lib::String arg_expression);
    std::map<::cw::lib::String, ::cw::lib::String> get_textdict();
};

class MouseWatcher : public ::cw::lib::Object {
public:
    MouseWatcher() = default;
    virtual ~MouseWatcher() = default;
    bool m__down{};
    ::cw::lib::ValueMap m__key_downs{};
    ::cw::lib::String m__last_mouse_button{};
    ::cw::lib::Value m__last_mouse_pos{};
    ::cw::lib::Value m__lock{};
    bool m__suppress_until_release{};
    bool m__touch{};
    bool m__was_foreground{};
    bool m_running{};
    mutable std::mutex m_state_mutex{};
    ::cw::lib::Value initialize();
    ::cw::lib::Value run();
    ::cw::lib::Value pop_touch(bool arg_nextlock);
    void release_next_lock();
    void reset();
};

class MouseWatcher_POINT : public ::cw::lib::Object {
public:
    MouseWatcher_POINT() = default;
    virtual ~MouseWatcher_POINT() = default;
    inline static std::vector<std::tuple<::cw::lib::String, ::cw::lib::Value>> s__fields_{};
};

class SetActionEventContent_POINT : public ::cw::lib::Object {
public:
    SetActionEventContent_POINT() = default;
    virtual ~SetActionEventContent_POINT() = default;
    inline static std::vector<std::tuple<::cw::lib::String, ::cw::lib::Value>> s__fields_{};
};

class ActionLinkStartContent : public ::cw::content::EventContentBase {
public:
    ActionLinkStartContent() = default;
    virtual ~ActionLinkStartContent() = default;
    ::cw::lib::Value m_startname{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class BranchContent : public ::cw::content::EventContentBase {
public:
    BranchContent() = default;
    virtual ~BranchContent() = default;
    inline static std::optional<::cw::lib::Value> s__boolean_table{};
    inline static std::optional<::cw::lib::Value> s__index_table{};
    bool m__boolean_checked{};
    ::cw::lib::Value m__boolean_table{};
    bool m__index_checked{};
    ::cw::lib::Value m__index_default{};
    ::cw::lib::ValueMap m__index_table{};
    bool m_invert{};
    ::cw::lib::Integer m_num{};
    ::cw::lib::Integer m_resid{};
    ::cw::lib::Value m_scope{};
    bool m_selectcard{};
    std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer>> m_boolean_cache{};
    std::vector<std::tuple<std::shared_ptr<::cw::content::EventContentBase>, bool>> m_boolean_checked_cache{};
    std::optional<std::map<::cw::lib::Integer, ::cw::lib::Integer>> m_index_cache{};
    std::vector<std::tuple<std::shared_ptr<::cw::content::EventContentBase>, ::cw::lib::Integer>> m_index_checked_cache{};
    bool m_boolean_cache_checked{};
    bool m_index_cache_checked{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer branch_cards(::cw::lib::String arg_cardtype);
    ::cw::lib::Integer get_boolean_index(bool arg_flag);
    ::cw::lib::Integer get_value_index(::cw::lib::Value arg_value);
    ::cw::lib::Integer get_compare_index(::cw::lib::Integer arg_cmptype);
};

class CallPackageContent : public ::cw::content::EventContentBase {
public:
    CallPackageContent() = default;
    virtual ~CallPackageContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class CallStartContent : public ::cw::content::EventContentBase {
public:
    CallStartContent() = default;
    virtual ~CallStartContent() = default;
    ::cw::lib::Value m_startname{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class ChangeAreaContent : public ::cw::content::EventContentBase {
public:
    ChangeAreaContent() = default;
    virtual ~ChangeAreaContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class ChangeBgImageContent : public ::cw::content::EventContentBase {
public:
    ChangeBgImageContent() = default;
    virtual ~ChangeBgImageContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class ChangeEnemyContent : public ::cw::content::EventContentBase {
public:
    ChangeEnemyContent() = default;
    virtual ~ChangeEnemyContent() = default;
    ::cw::lib::Value m_cardgroup{};
    ::cw::lib::Value m_castid{};
    ::cw::lib::Integer m_enemycard{};
    std::shared_ptr<::cw::data::CWPyElement> m_enemycard_typed{};
    bool m_regex{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data, bool arg_is_changestate);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class ChangeEnvironmentContent : public ::cw::content::EventContentBase {
public:
    ChangeEnvironmentContent() = default;
    virtual ~ChangeEnvironmentContent() = default;
    ::cw::lib::Value m_backpack{};
    ::cw::lib::Value m_gameover{};
    ::cw::lib::Value m_runaway{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class CheckFlagContent : public ::cw::content::EventContentBase {
public:
    CheckFlagContent() = default;
    virtual ~CheckFlagContent() = default;
    ::cw::lib::Value m_flag{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class CheckStepContent : public ::cw::content::EventContentBase {
public:
    CheckStepContent() = default;
    virtual ~CheckStepContent() = default;
    ::cw::lib::Value m_comparison{};
    ::cw::lib::Value m_step{};
    ::cw::lib::Integer m_value1{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class CheckVariantContent : public ::cw::content::EventContentBase {
public:
    CheckVariantContent() = default;
    virtual ~CheckVariantContent() = default;
    ::cw::lib::String m_expression{};
    std::optional<std::vector<::cw::lib::Value>> m_parsed_expression{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class EffectBreakContent : public ::cw::content::EventContentBase {
public:
    EffectBreakContent() = default;
    virtual ~EffectBreakContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class EffectContent : public ::cw::content::EventContentBase {
public:
    EffectContent() = default;
    virtual ~EffectContent() = default;
    std::shared_ptr<::cw::effectmotion::Effect> m_eff{};
    ::cw::lib::Value m_holdingcoupon{};
    bool m_ignite{};
    ::cw::lib::Integer m_initialchannel{};
    bool m_initialeffect{};
    ::cw::lib::Integer m_initialfadein{};
    ::cw::lib::Integer m_initialloopcount{};
    ::cw::lib::Value m_initialsoundpath{};
    ::cw::lib::Integer m_initialvolume{};
    ::cw::lib::Value m_keycodes{};
    ::cw::lib::Value m_target_var{};
    ::cw::lib::Value m_targetm{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class ElapseTimeContent : public ::cw::content::EventContentBase {
public:
    ElapseTimeContent() = default;
    virtual ~ElapseTimeContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class EndActionEventContent : public ::cw::content::EventContentBase {
public:
    EndActionEventContent() = default;
    virtual ~EndActionEventContent() = default;
    ::cw::lib::Value m_data{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class EndBadEndContent : public ::cw::content::EventContentBase {
public:
    EndBadEndContent() = default;
    virtual ~EndBadEndContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class EndContent : public ::cw::content::EventContentBase {
public:
    EndContent() = default;
    virtual ~EndContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class GetContent : public ::cw::content::EventContentBase {
public:
    GetContent() = default;
    virtual ~GetContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    void get_cards(::cw::lib::String arg_cardtype, std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_header_for_update_variables, std::optional<std::vector<std::shared_ptr<::cw::header::CardHeader>>> arg_ignore_list_for_update_variables);
};

class HidePartyContent : public ::cw::content::EventContentBase {
public:
    HidePartyContent() = default;
    virtual ~HidePartyContent() = default;
    ::cw::lib::Integer m_cardspeed{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class InputDialogContent : public ::cw::content::EventContentBase {
public:
    InputDialogContent() = default;
    virtual ~InputDialogContent() = default;
    ::cw::lib::Value m_message{};
    ::cw::lib::Value m_title{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data, ::cw::lib::String arg_title, ::cw::lib::String arg_message);
    ::cw::lib::Integer action();
};

class LinkPackageContent : public ::cw::content::EventContentBase {
public:
    LinkPackageContent() = default;
    virtual ~LinkPackageContent() = default;
    ::cw::lib::Integer m_resid{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class LinkStartContent : public ::cw::content::EventContentBase {
public:
    LinkStartContent() = default;
    virtual ~LinkStartContent() = default;
    ::cw::lib::Value m_startname{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class LoseBgImageContent : public ::cw::content::EventContentBase {
public:
    LoseBgImageContent() = default;
    virtual ~LoseBgImageContent() = default;
    ::cw::lib::Value m_cellname{};
    bool m_doanime{};
    bool m_ignoreeffectbooster{};
    bool m_regex{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class LoseContent : public ::cw::content::EventContentBase {
public:
    LoseContent() = default;
    virtual ~LoseContent() = default;
    ::cw::lib::Integer m_num{};
    ::cw::lib::Integer m_resid{};
    ::cw::lib::Value m_scope{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    void lose_cards(::cw::lib::String arg_cardtype);
    std::tuple<std::vector<std::shared_ptr<::cw::header::CardHeader>>, ::cw::lib::Integer> lose_card(::cw::lib::String arg_name, ::cw::lib::String arg_desc, std::vector<std::shared_ptr<::cw::header::CardHeader>> arg_target, ::cw::lib::Integer arg_num);
};

class MoveBgImageContent : public ::cw::content::EventContentBase {
public:
    MoveBgImageContent() = default;
    virtual ~MoveBgImageContent() = default;
    ::cw::lib::Value m_cellname{};
    bool m_doanime{};
    ::cw::lib::Integer m_height{};
    bool m_ignoreeffectbooster{};
    ::cw::lib::Value m_positiontype{};
    bool m_regex{};
    ::cw::lib::Value m_sizetype{};
    ::cw::lib::Integer m_width{};
    ::cw::lib::Integer m_x{};
    ::cw::lib::Integer m_y{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data, bool arg_is_changestate);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class MoveCardContent : public ::cw::content::EventContentBase {
public:
    MoveCardContent() = default;
    virtual ~MoveCardContent() = default;
    ::cw::lib::Value m_cardgroup{};
    ::cw::lib::Integer m_cardspeed{};
    ::cw::lib::Integer m_height{};
    ::cw::lib::Integer m_layer{};
    bool m_overridecardspeed{};
    ::cw::lib::Value m_positiontype{};
    ::cw::lib::Integer m_scale{};
    ::cw::lib::Integer m_width{};
    ::cw::lib::Integer m_x{};
    ::cw::lib::Integer m_y{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data, bool arg_is_changestate);
    ::cw::lib::Integer action();
    ::cw::lib::Integer _do_action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class MultipleMoveLoseBgImageContent : public ::cw::content::EventContentBase {
public:
    MultipleMoveLoseBgImageContent() = default;
    virtual ~MultipleMoveLoseBgImageContent() = default;
    bool m_doanime{};
    bool m_ignoreeffectbooster{};
    std::vector<::cw::lib::String> m_loses{};
    std::vector<std::tuple<::cw::lib::String, std::tuple<::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::String, ::cw::lib::Integer, ::cw::lib::Integer>>> m_moves{};
    bool m_regex{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data, bool arg_is_changestate);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class PlayBgmContent : public ::cw::content::EventContentBase {
public:
    PlayBgmContent() = default;
    virtual ~PlayBgmContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class PlaySoundContent : public ::cw::content::EventContentBase {
public:
    PlaySoundContent() = default;
    virtual ~PlaySoundContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class PostEventContent : public ::cw::content::EventContentBase {
public:
    PostEventContent() = default;
    virtual ~PostEventContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    static void do_action(::cw::lib::String arg_command, std::optional<::cw::lib::String> arg_arg);
};

class RedisplayContent : public ::cw::content::EventContentBase {
public:
    RedisplayContent() = default;
    virtual ~RedisplayContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class ReplaceBgImageContent : public ::cw::content::EventContentBase {
public:
    ReplaceBgImageContent() = default;
    virtual ~ReplaceBgImageContent() = default;
    ::cw::lib::Value m_cellname{};
    bool m_doanime{};
    bool m_ignoreeffectbooster{};
    bool m_regex{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class ReverseFlagContent : public ::cw::content::EventContentBase {
public:
    ReverseFlagContent() = default;
    virtual ~ReverseFlagContent() = default;
    ::cw::lib::Integer m_cardspeed{};
    ::cw::lib::Value m_flag{};
    bool m_overridecardspeed{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class SetActionEventContent : public ::cw::content::EventContentBase {
public:
    SetActionEventContent() = default;
    virtual ~SetActionEventContent() = default;
    ::cw::lib::Value m_data{};
    ::cw::lib::Value m_type{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class SetFlagContent : public ::cw::content::EventContentBase {
public:
    SetFlagContent() = default;
    virtual ~SetFlagContent() = default;
    ::cw::lib::Integer m_cardspeed{};
    ::cw::lib::Value m_flag{};
    bool m_overridecardspeed{};
    bool m_value{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class SetStepContent : public ::cw::content::EventContentBase {
public:
    SetStepContent() = default;
    virtual ~SetStepContent() = default;
    ::cw::lib::Value m_step{};
    ::cw::lib::Integer m_value{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class SetStepDownContent : public ::cw::content::EventContentBase {
public:
    SetStepDownContent() = default;
    virtual ~SetStepDownContent() = default;
    ::cw::lib::Value m_step{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class SetStepUpContent : public ::cw::content::EventContentBase {
public:
    SetStepUpContent() = default;
    virtual ~SetStepUpContent() = default;
    ::cw::lib::Value m_step{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class ShowPartyContent : public ::cw::content::EventContentBase {
public:
    ShowPartyContent() = default;
    virtual ~ShowPartyContent() = default;
    ::cw::lib::Integer m_cardspeed{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class StartBattleContent : public ::cw::content::EventContentBase {
public:
    StartBattleContent() = default;
    virtual ~StartBattleContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class StartContent : public ::cw::content::EventContentBase {
public:
    StartContent() = default;
    virtual ~StartContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class SubstituteFlagContent : public ::cw::content::EventContentBase {
public:
    SubstituteFlagContent() = default;
    virtual ~SubstituteFlagContent() = default;
    ::cw::lib::Integer m_cardspeed{};
    ::cw::lib::Value m_fromflag{};
    bool m_overridecardspeed{};
    ::cw::lib::Value m_toflag{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class SubstituteStepContent : public ::cw::content::EventContentBase {
public:
    SubstituteStepContent() = default;
    virtual ~SubstituteStepContent() = default;
    ::cw::lib::Value m_fromstep{};
    ::cw::lib::Value m_tostep{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class TalkContent : public ::cw::content::EventContentBase {
public:
    TalkContent() = default;
    virtual ~TalkContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    std::vector<std::tuple<::cw::lib::Integer, ::cw::lib::String>> get_selections_and_indexes();
};

class WaitContent : public ::cw::content::EventContentBase {
public:
    WaitContent() = default;
    virtual ~WaitContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class XmlContentCustomContent : public ::cw::content::EventContentBase {
public:
    XmlContentCustomContent() = default;
    virtual ~XmlContentCustomContent() = default;
    ::cw::lib::Integer m_assignment{};
    ::cw::lib::Value m_xmltype{};
    std::shared_ptr<::cw::data::CWPyElement> m_str_elem{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class BranchAbilityContent : public ::cw::content::BranchContent {
public:
    BranchAbilityContent() = default;
    virtual ~BranchAbilityContent() = default;
    bool m_invert{};
    ::cw::lib::Integer m_level{};
    bool m_sleep{};
    bool m_someone{};
    ::cw::lib::String m_targetm{};
    std::tuple<::cw::lib::Value, ::cw::lib::Value> m_vocation{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    ::cw::lib::String get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event);
};

class BranchAreaContent : public ::cw::content::BranchContent {
public:
    BranchAreaContent() = default;
    virtual ~BranchAreaContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    ::cw::lib::String get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event);
};

class BranchBattleContent : public ::cw::content::BranchContent {
public:
    BranchBattleContent() = default;
    virtual ~BranchBattleContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    ::cw::lib::String get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event);
};

class BranchBeastContent : public ::cw::content::BranchContent {
public:
    BranchBeastContent() = default;
    virtual ~BranchBeastContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    ::cw::lib::String get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event);
};

class BranchCastContent : public ::cw::content::BranchContent {
public:
    BranchCastContent() = default;
    virtual ~BranchCastContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    ::cw::lib::String get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event);
};

class BranchCompleteStampContent : public ::cw::content::BranchContent {
public:
    BranchCompleteStampContent() = default;
    virtual ~BranchCompleteStampContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    ::cw::lib::String get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event);
};

class BranchCouponContent : public ::cw::content::BranchContent {
public:
    BranchCouponContent() = default;
    virtual ~BranchCouponContent() = default;
    ::cw::lib::Value m_couponnames{};
    bool m_invert{};
    ::cw::lib::Value m_matchingtype{};
    ::cw::lib::Value m_result_var{};
    ::cw::lib::Value m_scope{};
    ::cw::lib::Value m_someone{};
    bool m_spchars{};
    ::cw::lib::Value m_unreversed{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    ::cw::lib::String get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event);
};

class BranchFlagContent : public ::cw::content::BranchContent {
public:
    BranchFlagContent() = default;
    virtual ~BranchFlagContent() = default;
    ::cw::lib::Value m_flag{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    ::cw::lib::String get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event);
};

class BranchFlagValueContent : public ::cw::content::BranchContent {
public:
    BranchFlagValueContent() = default;
    virtual ~BranchFlagValueContent() = default;
    ::cw::lib::Value m_fromflag{};
    ::cw::lib::Value m_toflag{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    ::cw::lib::String get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event);
};

class BranchGossipContent : public ::cw::content::BranchContent {
public:
    BranchGossipContent() = default;
    virtual ~BranchGossipContent() = default;
    ::cw::lib::Value m_gossip{};
    bool m_spchars{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    ::cw::lib::String get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event);
};

class BranchInfoContent : public ::cw::content::BranchContent {
public:
    BranchInfoContent() = default;
    virtual ~BranchInfoContent() = default;
    ::cw::lib::Integer m_resid{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    ::cw::lib::String get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event);
};

class BranchIsBattleContent : public ::cw::content::BranchContent {
public:
    BranchIsBattleContent() = default;
    virtual ~BranchIsBattleContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    ::cw::lib::String get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event);
};

class BranchItemContent : public ::cw::content::BranchContent {
public:
    BranchItemContent() = default;
    virtual ~BranchItemContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    ::cw::lib::String get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event);
};

class BranchKeyCodeContent : public ::cw::content::BranchContent {
public:
    BranchKeyCodeContent() = default;
    virtual ~BranchKeyCodeContent() = default;
    bool m_beast{};
    ::cw::lib::Value m_condition{};
    bool m_hand{};
    bool m_invert{};
    bool m_item{};
    ::cw::lib::Value m_keycode{};
    ::cw::lib::Value m_result_var{};
    bool m_selectcard{};
    bool m_skill{};
    ::cw::lib::Value m_targetkc{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    bool _matches_keycode(std::shared_ptr<::cw::header::CardHeader> arg_header);
    std::vector<std::shared_ptr<::cw::header::CardHeader>> _find_keycode_cards(::cw::lib::Value arg_target);
    static std::optional<std::shared_ptr<::cw::data::StructVal>> _cardinfo_from_header(std::shared_ptr<::cw::header::CardHeader> arg_header);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    ::cw::lib::String get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event);
};

class BranchLevelContent : public ::cw::content::BranchContent {
public:
    BranchLevelContent() = default;
    virtual ~BranchLevelContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    ::cw::lib::String get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event);
};

class BranchMoneyContent : public ::cw::content::BranchContent {
public:
    BranchMoneyContent() = default;
    virtual ~BranchMoneyContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    ::cw::lib::String get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event);
};

class BranchMultiCouponContent : public ::cw::content::BranchContent {
public:
    BranchMultiCouponContent() = default;
    virtual ~BranchMultiCouponContent() = default;
    ::cw::lib::Value m_scope{};
    ::cw::lib::Value m_someone{};
    bool m_spchars{};
    ::cw::lib::Value m_unreversed{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    ::cw::lib::String get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event);
};

class BranchMultiRandomContent : public ::cw::content::BranchContent {
public:
    BranchMultiRandomContent() = default;
    virtual ~BranchMultiRandomContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class BranchMultiStepContent : public ::cw::content::BranchContent {
public:
    BranchMultiStepContent() = default;
    virtual ~BranchMultiStepContent() = default;
    ::cw::lib::Value m_nextlen{};
    ::cw::lib::Value m_step{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    ::cw::lib::String get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event);
};

class BranchPartyNumberContent : public ::cw::content::BranchContent {
public:
    BranchPartyNumberContent() = default;
    virtual ~BranchPartyNumberContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    ::cw::lib::String get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event);
};

class BranchRandomContent : public ::cw::content::BranchContent {
public:
    BranchRandomContent() = default;
    virtual ~BranchRandomContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    ::cw::lib::String get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event);
};

class BranchRandomSelectContent : public ::cw::content::BranchContent {
public:
    BranchRandomSelectContent() = default;
    virtual ~BranchRandomSelectContent() = default;
    bool m_invert{};
    ::cw::lib::Value m_maxlevel{};
    ::cw::lib::Value m_minlevel{};
    ::cw::lib::Value m_ranges{};
    ::cw::lib::Value m_status{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    std::set<::cw::lib::String> get_castranges();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    ::cw::lib::String get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event);
};

class BranchRoundContent : public ::cw::content::BranchContent {
public:
    BranchRoundContent() = default;
    virtual ~BranchRoundContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    ::cw::lib::String get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event);
};

class BranchSelectContent : public ::cw::content::BranchContent {
public:
    BranchSelectContent() = default;
    virtual ~BranchSelectContent() = default;
    bool m__init_values{};
    ::cw::lib::String m_method{};
    bool m_targetall{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    ::cw::lib::String get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event);
};

class BranchSkillContent : public ::cw::content::BranchContent {
public:
    BranchSkillContent() = default;
    virtual ~BranchSkillContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    ::cw::lib::String get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event);
};

class BranchStatusContent : public ::cw::content::BranchContent {
public:
    BranchStatusContent() = default;
    virtual ~BranchStatusContent() = default;
    ::cw::lib::Value m_andor{};
    bool m_invert{};
    ::cw::lib::Value m_status{};
    ::cw::lib::Value m_target_var{};
    ::cw::lib::Value m_targetm{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    ::cw::lib::String get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event);
};

class BranchStepContent : public ::cw::content::BranchContent {
public:
    BranchStepContent() = default;
    virtual ~BranchStepContent() = default;
    ::cw::lib::Value m_nextlen{};
    ::cw::lib::Value m_step{};
    ::cw::lib::Integer m_value{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    ::cw::lib::String get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event);
};

class BranchStepValueContent : public ::cw::content::BranchContent {
public:
    BranchStepValueContent() = default;
    virtual ~BranchStepValueContent() = default;
    ::cw::lib::Value m_fromstep{};
    ::cw::lib::Value m_tostep{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    ::cw::lib::String get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event);
};

class BranchVariantContent : public ::cw::content::BranchContent {
public:
    BranchVariantContent() = default;
    virtual ~BranchVariantContent() = default;
    ::cw::lib::String m_expression{};
    std::optional<std::vector<::cw::lib::Value>> m_parsed_expression{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
    ::cw::lib::String get_childname(std::shared_ptr<::cw::data::CWPyElement> arg_child, std::shared_ptr<::cw::event::Event> arg_event);
};

class GetBeastContent : public ::cw::content::GetContent {
public:
    GetBeastContent() = default;
    virtual ~GetBeastContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class GetCastContent : public ::cw::content::GetContent {
public:
    GetCastContent() = default;
    virtual ~GetCastContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class GetCompleteStampContent : public ::cw::content::GetContent {
public:
    GetCompleteStampContent() = default;
    virtual ~GetCompleteStampContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class GetCouponContent : public ::cw::content::GetContent {
public:
    GetCouponContent() = default;
    virtual ~GetCouponContent() = default;
    ::cw::lib::Value m_coupon{};
    ::cw::lib::Value m_holdingcoupon{};
    ::cw::lib::Value m_scope{};
    bool m_spchars{};
    ::cw::lib::Integer m_value{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class GetGossipContent : public ::cw::content::GetContent {
public:
    GetGossipContent() = default;
    virtual ~GetGossipContent() = default;
    ::cw::lib::Value m_gossip{};
    bool m_spchars{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class GetInfoContent : public ::cw::content::GetContent {
public:
    GetInfoContent() = default;
    virtual ~GetInfoContent() = default;
    ::cw::lib::Integer m_resid{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class GetItemContent : public ::cw::content::GetContent {
public:
    GetItemContent() = default;
    virtual ~GetItemContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class GetMoneyContent : public ::cw::content::GetContent {
public:
    GetMoneyContent() = default;
    virtual ~GetMoneyContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class GetSkillContent : public ::cw::content::GetContent {
public:
    GetSkillContent() = default;
    virtual ~GetSkillContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class LoseBeastContent : public ::cw::content::LoseContent {
public:
    LoseBeastContent() = default;
    virtual ~LoseBeastContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class LoseCastContent : public ::cw::content::LoseContent {
public:
    LoseCastContent() = default;
    virtual ~LoseCastContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class LoseCompleteStampContent : public ::cw::content::LoseContent {
public:
    LoseCompleteStampContent() = default;
    virtual ~LoseCompleteStampContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class LoseCouponContent : public ::cw::content::LoseContent {
public:
    LoseCouponContent() = default;
    virtual ~LoseCouponContent() = default;
    ::cw::lib::Value m_coupon{};
    ::cw::lib::Value m_holdingcoupon{};
    ::cw::lib::Value m_scope{};
    bool m_spchars{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class LoseGossipContent : public ::cw::content::LoseContent {
public:
    LoseGossipContent() = default;
    virtual ~LoseGossipContent() = default;
    ::cw::lib::Value m_gossip{};
    bool m_spchars{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class LoseInfoContent : public ::cw::content::LoseContent {
public:
    LoseInfoContent() = default;
    virtual ~LoseInfoContent() = default;
    ::cw::lib::Value m_name{};
    // Pythonのresidは一つだけなので、LoseContentが初期化したIDを使
    // う。 ここで同名メンバーを追加すると元の値が隠れ、誤ってID 0を対象にしてしまう。
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class LoseItemContent : public ::cw::content::LoseContent {
public:
    LoseItemContent() = default;
    virtual ~LoseItemContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class LoseMoneyContent : public ::cw::content::LoseContent {
public:
    LoseMoneyContent() = default;
    virtual ~LoseMoneyContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class LoseSkillContent : public ::cw::content::LoseContent {
public:
    LoseSkillContent() = default;
    virtual ~LoseSkillContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class SetVariantContent : public ::cw::content::BranchContent {
public:
    SetVariantContent() = default;
    virtual ~SetVariantContent() = default;
    ::cw::lib::String m_expression{};
    ::cw::lib::Value m_flag{};
    std::optional<std::vector<::cw::lib::Value>> m_parsed_expression{};
    ::cw::lib::Value m_step{};
    ::cw::lib::Value m_variant{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class TalkDialogContent : public ::cw::content::TalkContent {
public:
    TalkDialogContent() = default;
    virtual ~TalkDialogContent() = default;
    bool m__init_values{};
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    bool can_action();
    std::vector<std::tuple<std::vector<::cw::lib::String>, ::cw::lib::String>> get_dialogs();
    std::optional<::cw::lib::String> get_dialogtext(std::vector<std::tuple<std::vector<::cw::lib::String>, ::cw::lib::String>> arg_dialogs, std::vector<::cw::lib::String> arg_coupons);
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

class TalkMessageContent : public ::cw::content::TalkContent {
public:
    TalkMessageContent() = default;
    virtual ~TalkMessageContent() = default;
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    ::cw::lib::Integer action();
    bool can_action();
    ::cw::lib::String get_status(std::optional<std::shared_ptr<::cw::event::Event>> arg_event);
};

extern ::cw::lib::Integer g_VK_LBUTTON;
extern std::map<::cw::lib::String, ::cw::lib::String> g_methoddict;
extern std::shared_ptr<::cw::content::MouseWatcher> g_mosuewatcher;

::cw::lib::String variant_error_msg(std::shared_ptr<::cw::calculator::ComputeException> arg_ex);
::cw::lib::String variant_error_msg(const ::cw::calculator::ComputeException& arg_ex);
// Pythonの局所クロージャーに対応する、生成済みAPI一覧用の互換シンボル。 実行時の収集処理
// はBranchKeyCodeContent::_find_keycode_cards()が行う。
void BranchKeyCodeContent__find_keycode_cards_append_card(std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_header);
std::tuple<::cw::lib::String, bool, bool> _get_couponscope(::cw::lib::String arg_scope);
bool _target_has_coupon(std::shared_ptr<::cw::character::Character> arg_target, std::vector<::cw::lib::String> arg_names, bool arg_allmatch);
bool _target_has_coupon_has_coupon(::cw::lib::String arg_coupon);
std::vector<std::shared_ptr<::cw::character::Character>> _get_coupon_owners(std::vector<std::shared_ptr<::cw::character::Character>> arg_targets, std::vector<::cw::lib::String> arg_names, bool arg_allmatch);
bool _has_coupon(std::vector<std::shared_ptr<::cw::character::Character>> arg_targets, std::vector<::cw::lib::String> arg_names, ::cw::lib::String arg_scope, bool arg_someone, bool arg_allmatch, bool arg_multi, bool arg_invert);
bool _has_coupon_has_coupon(std::shared_ptr<::cw::character::Character> arg_target, ::cw::lib::String arg_coupon);
bool call_package(::cw::lib::Integer arg_resid, bool arg_call);
void ChangeAreaContent_action_change_area(::cw::lib::Integer arg_resid, std::tuple<::cw::lib::String, ::cw::lib::Value> arg_ttype);
bool check_areaid(::cw::lib::Integer arg_resid);
std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>> EffectContent_action_initial_effect(std::vector<std::shared_ptr<::cw::sprite::card::CWPyCard>> arg_targets, std::optional<std::shared_ptr<::cw::event::Targeting>> arg_tevent);
void EffectContent_action_initial_effect_remove_target(std::shared_ptr<::cw::sprite::card::CWPyCard> arg_target);
void EffectContent_action_apply(std::shared_ptr<::cw::sprite::card::CWPyCard> arg_target);
void end_scenario(bool arg_complete);
bool GetContent_get_cards_is_nocache(::cw::lib::Value arg_target);
void get_card(std::shared_ptr<::cw::data::CWPyElementTree> arg_etree, ::cw::lib::Value arg_target, bool arg_notscenariocard, ::cw::lib::Integer arg_toindex, ::cw::lib::Integer arg_insertorder, std::optional<std::shared_ptr<::cw::data::Party>> arg_party, ::cw::lib::String arg_copymaterialfrom, bool arg_fromdebugger, bool arg_from_getcontent, bool arg_attachment, bool arg_update_image, bool arg_anotherscenariocard, std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_header_for_update_variables, std::optional<std::vector<std::shared_ptr<::cw::header::CardHeader>>> arg_ignore_list_for_update_variables);
bool is_addablecoupon(::cw::lib::String arg_coupon);
::cw::lib::Value is_xmltype_check(::cw::lib::Value arg_e);
::cw::lib::String _replace_variant_placeholders(::cw::lib::String arg_text);
void _copy_element_context(std::shared_ptr<::cw::data::CWPyElement> arg_src, std::shared_ptr<::cw::data::CWPyElement> arg_dst);
::cw::lib::Value clean_xmltext(::cw::lib::Value arg_xmltext);
std::optional<::cw::lib::String> InputDialogContent_action_show_dialog(
    ::cw::lib::String arg_title, ::cw::lib::String arg_message,
    ::cw::lib::String arg_initial,
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size);
std::shared_ptr<::cw::content::EventContentBase> get_content(std::shared_ptr<::cw::data::CWPyElement> arg_data);
void main();
}

