#include "cw/battle.hpp"

#include "cw/advlog.hpp"
#include "cw/character.hpp"
#include "cw/cw.hpp"
#include "cw/data.hpp"
#include "cw/deck.hpp"
#include "cw/event.hpp"
#include "cw/eventhandler.hpp"
#include "cw/header.hpp"
#include "cw/sprite/card.hpp"
#include "cw/sprite/statusbar.hpp"
#include "cw/sprite/base.hpp"
#include "cw/setting.hpp"
#include "cw/thread.hpp"

#include <algorithm>
#include <stdexcept>

namespace {
using Integer = ::cw::lib::Integer;
using Value = ::cw::lib::Value;
using Character = ::cw::character::Character;

std::shared_ptr<::cw::thread::CWPy> runtime()
{
    return ::cw::g_cwpy;
}

std::shared_ptr<::cw::data::ScenarioData> scenario_data()
{
    const auto app = runtime();
    if (!app) return nullptr;
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&app->m_sdata.storage());
    return object && *object ? std::dynamic_pointer_cast<::cw::data::ScenarioData>(*object) : nullptr;
}

std::shared_ptr<::cw::event::EventEngine> scenario_events()
{
    const auto data = scenario_data();
    return data && data->m_events ? *data->m_events : nullptr;
}

std::shared_ptr<::cw::battle::BattleEngine> self_battle(::cw::battle::BattleEngine* battle)
{
    const auto app = runtime();
    if (app && app->m_battle && *app->m_battle && app->m_battle->get() == battle)
        return *app->m_battle;
    return nullptr;
}

bool playing_scenario()
{
    const auto app = runtime();
    return app && app->is_playingscenario();
}

bool scenario_f9()
{
    const auto data = scenario_data();
    return data && data->m_in_f9;
}

void start_event(Integer keynum)
{
    if (const auto data = scenario_data()) data->start_event(keynum, std::nullopt, true);
}

void clear_battle_event_state()
{
    if (const auto app = runtime()) app->clear_selection();
}

void remove_menu_cards_from_group(const std::shared_ptr<::cw::thread::CWPy>& app)
{
    if (!app) return;
    // Pythonの cw.cwpy.cardgrp.remove(*cw.cwpy.mcards) と同様、spriteそのものの同一性で除く。
    for (const auto& value : app->m_mcards) {
        const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
        const auto card = object && *object ? std::dynamic_pointer_cast<::cw::sprite::card::CWPyCard>(*object) : nullptr;
        if (!card) continue;
        ::cw::sprite::card::remove_from_cardgrp(*card);
        if (card->m_ui_backend) card->m_ui_backend->unregister_sprite(*card);
        else if (const auto backend = ::cw::sprite::base::sprite_ui_backend())
            backend->unregister_sprite(*card);
    }
}

template <typename Card>
std::shared_ptr<Character> as_character(const std::shared_ptr<Card>& card)
{
    return card ? std::dynamic_pointer_cast<Character>(card) : nullptr;
}

}

namespace cw::battle {
void BattleEngine::initialize_card_variables(::cw::lib::String arg_initialization)
{
    const auto app = runtime();
    if (!app) return;
    const auto players = app->get_pcards(Value{});
    const auto friends = app->get_fcards(Value{});
    auto initialize = [&arg_initialization](const auto& cards) {
        for (const auto& card : cards) {
            const auto character = as_character(card);
            if (!character) continue;
            for (Integer pocket = 0; pocket < 3; ++pocket) {
                for (const auto& header : character->get_pocketcards(pocket)) if (header) header->initialize_variables(arg_initialization);
            }
        }
    };
    initialize(players);
    initialize(friends);
}

void BattleEngine::initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data)
{
    m_data_typed = std::move(arg_data);
    m_data = m_data_typed ? Value(std::static_pointer_cast<::cw::lib::Object>(m_data_typed)) : Value{};
    m_round = 0;
    m_members.clear();
    m_pmembers.clear();
    m_emembers.clear();
    m_fmembers.clear();
    m_priorityacts.clear();
    m_priorityacts_beast.clear();
    m__ready = false;
    m__running = false;
    m__ranaway = false;
    m_in_roundevent = false;
    m_numenemy = 0;
    m_possible_runaway = m_data_typed ? m_data_typed->getbool("Property/RunAway", std::nullopt, true) : true;
    if (const auto app = runtime()) {
        for (const auto& player : app->get_pcards(Value{})) {
            const auto character = as_character(player);
            if (character && character->m_deck) character->m_deck->set(character, false);
        }
        for (const auto& companion : app->get_fcards(Value{})) {
            const auto character = as_character(companion);
            if (character && character->m_deck) character->m_deck->set(character, false);
        }
        if (app->is_autospread()) m_numenemy = static_cast<Integer>(app->get_mcards(Value("flagtrue"), "").size());
        // CWPy::m_battle は呼び出し元が initialize()
        // 前に設定する。 初期化の例外でこのオブジェクトよ
        // り長く残るため、ここで非所有の別名を公開しない。
        if (!app->m_battle || !*app->m_battle || app->m_battle->get() != this)
            throw std::logic_error("BattleEngine::initialize requires an owned active battle");
        initialize_card_variables("BattleStart");
        try {
            if (app->m_advlog) app->m_advlog->start_battle(*app->m_battle);
            start_event(5);
            ready();
        } catch (const BattleStartBattleError&) {
            end(false, false, true);
        } catch (const BattleAreaChangeError&) {
            end(false);
        } catch (const BattleWinError&) {
            win(false);
        } catch (const BattleDefeatError&) {
            defeat(false);
        }
    } else throw std::logic_error("BattleEngine::initialize requires an active CWPy runtime");
}

bool BattleEngine::is_running()
{
    return m__running;
}

bool BattleEngine::is_ready()
{
    return m__ready;
}

bool BattleEngine::is_battlestarting()
{
    return !is_running() && !is_ready();
}

void BattleEngine::start()
{
    // Pythonのバインドされたメソッドは、入力・イベントのコールバックが cwpy.battle
    // を置き換えてもselfを保持する。 runと各処理の間、同じ寿命を維持する。
    const auto keep_alive = self_battle(this);
    try {
        run();
    } catch (const BattleStartBattleError&) {
        end(false, false, true);
    } catch (const BattleAreaChangeError&) {
        end(false);
    } catch (const BattleWinError&) {
        win();
    } catch (const BattleDefeatError&) {
        defeat();
    }
}

void BattleEngine::process_exception(std::shared_ptr<::cw::battle::BattleError> arg_ex)
{
    if (std::dynamic_pointer_cast<BattleStartBattleError>(arg_ex)) end(false, false, true);
    else if (std::dynamic_pointer_cast<BattleAreaChangeError>(arg_ex)) end(false);
    else if (std::dynamic_pointer_cast<BattleWinError>(arg_ex)) win();
    else if (std::dynamic_pointer_cast<BattleDefeatError>(arg_ex)) defeat();
    else throw std::logic_error("unknown battle exception");
}

void BattleEngine::run()
{
    const auto keep_alive = self_battle(this);
    const auto app = runtime();
    if (app) {
        app->clear_selection();
        app->clear_fcardsprites();
        if (!playing_scenario() || scenario_f9()) { end(true, true, false); return; }
    }
    m__running = true;
    m__ready = false;
    if (check_defeat()) throw BattleDefeatError();
    if (check_win()) throw BattleWinError();
    if (app && app->m_advlog) app->m_advlog->start_round(m_round);
    m_in_roundevent = true;
    try { start_event(-m_round); }
    catch (...) { m_in_roundevent = false; throw; }
    m_in_roundevent = false;
    if (app && (!playing_scenario() || scenario_f9())) { end(true, true, false); return; }
    if (check_defeat()) throw BattleDefeatError();
    if (check_win()) throw BattleWinError();
    if (app) {
        if (const auto clip = app->update_statusimgs(true, std::nullopt); clip && *clip) app->add_lazydraw(*clip);
    }
    for (auto& member : m_members) if (member) member->m_actionend = false;
    {
        // Pythonは set_members/actionorder が self.members
        // を置き換えても、走査中のリストを保持する。 置換まではその場の追加
        // ・削除が走査に影響するため、開始時に無条件で複製するのは正しくない。
        const auto iteration = std::make_shared<MemberIteration>();
        iteration->previous = m_member_iteration;
        m_member_iteration = iteration;
        struct RestoreIteration {
            std::shared_ptr<MemberIteration>& slot;
            std::shared_ptr<MemberIteration> previous;
            ~RestoreIteration() { slot = std::move(previous); }
        } restore{m_member_iteration, iteration->previous};
        const auto members = [&]() -> const Members& {
            return iteration->retained ? *iteration->retained : m_members;
        };
        for (std::size_t index = 0; index < members().size(); ++index) {
            const auto member = members()[index];
            if (!member || member->m_actionend) continue;
            if (app) {
                app->input(false, false, false);
                if (const auto handler = app->get_eventhandler()) handler->run();
            }
            member->action();
            if (app && (!playing_scenario() || scenario_f9())) { end(true, true, false); return; }
            if (!m__running) return;
            if (check_win()) throw BattleWinError();
        }
    }
    start_event(6);
    if (app && (!playing_scenario() || scenario_f9())) { end(true, true, false); return; }
    if (check_defeat()) throw BattleDefeatError();
    if (check_win()) throw BattleWinError();
    for (auto& member : m_members) {
        if (!member) continue;
        if (member->m_deck) member->m_deck->clear_used();
        member->clear_action();
    }
    if (app && (!playing_scenario() || scenario_f9())) { end(true, true, false); return; }
    if (app) {
        app->input(false, false, false);
        if (const auto handler = app->get_eventhandler()) handler->run();
        app->elapse_time(false, false);
        if (!app->is_battlestatus() || !app->m_battle || app->m_battle->get() != this) return;
    }
    if (check_defeat()) throw BattleDefeatError();
    if (check_win()) throw BattleWinError();
    const auto ran = m__ranaway;
    m__running = false;
    m__ranaway = false;
    const auto data = scenario_data();
    if (app && app->m_setting && data && data->m_autostart_round && app->m_setting->m_show_roundautostartbutton && !ran) {
        ready(false);
        const auto self = self_battle(this);
        if (self) app->exec_func([self](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) { self->start(); return Value{}; });
        else start();
    } else {
        ready();
    }
}

void BattleEngine::end(bool arg_areachange, bool arg_f9, bool arg_startnextbattle)
{
    // 現行のReboot版の BattleEngine.end と同様、実行中でなくても
    // 後処理を行う。 無条件の後処理を保ち、リセット前に starting を取得する。
    const auto app = runtime();
    if (app) {
        app->clean_specials(true, false);
        for (const auto& member : app->get_pcards(Value{})) if (member) { if (auto card = as_character(member)) card->clear_action(); }
    }
    const auto starting = is_battlestarting();
    m__running = false;
    m__ready = true;
    if (app) {
        if (arg_f9) {
            if (const auto data = scenario_data()) data->m_pre_battleareadata = std::nullopt;
        } else {
            const auto self = self_battle(this);
            if (!arg_startnextbattle && self && app->m_advlog) app->m_advlog->end_battle(self);
            app->clear_battlearea(arg_areachange, 0, arg_startnextbattle, starting);
        }
    }
}

void BattleEngine::ready(bool arg_redraw)
{
    // Python版は上限のない整数を増やしてから1～999999へ
    // 制限する。 ネイティブ版は増加前に制限し、Integerの最
    // 大値が渡されても符号付きオーバーフローなしで同じ結果にする。
    m_round = std::clamp<Integer>(m_round, 0, 999998) + 1;
    clear_priorityacts();
    set_members();
    for (const auto& member : m_members) if (member && member->m_deck) member->m_deck->draw(member);
    set_action();
    set_actionorder();
    if (const auto app = runtime()) {
        if (app->is_autospread()) {
            const auto ecards = app->get_mcards(Value("flagtrue"), "");
            if (m_numenemy != static_cast<Integer>(ecards.size())) {
                m_numenemy = static_cast<Integer>(ecards.size());
                if (const auto data = scenario_data()) {
                    data->m_moved_mcards.clear();
                    app->set_autospread(ecards, 6, false, true);
                }
            }
        }
        app->show_party();
        app->disposition_pcards();
        if (app->is_debugmode() && app->m_setting && app->m_setting->m_show_fcardsinbattle) app->add_fcardsprites("normal", 192);
        if (arg_redraw) {
            m__ready = true;
            if (app->m_statusbar) app->m_statusbar->change(true, false);
            app->clear_selection();
            app->update_allselectedcards();
        }
    } else if (arg_redraw) {
        m__ready = true;
    }
}

void BattleEngine::update_debug()
{
    const auto app = runtime();
    if (!app) return;
    for (const auto& value : app->get_mcards(Value{}, "")) {
        if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage()); object && *object) {
            if (const auto card = std::dynamic_pointer_cast<::cw::sprite::card::CWPyCard>(*object)) card->update_scale();
        }
    }
    if (is_ready()) update_showfcards();
}

void BattleEngine::update_showfcards()
{
    const auto app = runtime();
    if (!app) return;
    app->clear_fcardsprites();
    if (app->is_debugmode() && app->m_setting && app->m_setting->m_show_fcardsinbattle && is_ready()) app->add_fcardsprites("normal", 192);
}

void BattleEngine::runaway()
{
    const auto app = runtime();
    if (!app) return;
    app->clear_fcardsprites();
    clear_playersaction();
    m__ranaway = true;
    if (app->m_advlog) app->m_advlog->start_runaway();
    const auto events = scenario_events();
    const auto event = events ? events->check_keynum(2) : std::nullopt;
    if (event) {
        bool restart = true;
        try {
            app->clear_selection();
            app->clear_fcardsprites();
            m__ready = false;
            m__running = true;
            start_event(2);
        } catch (const BattleStartBattleError&) {
            end(false, false, true);
            restart = false;
        } catch (const BattleAreaChangeError&) {
            end(false);
            restart = false;
        } catch (const BattleDefeatError&) {
            defeat();
            restart = false;
        } catch (const BattleError&) {
            restart = true;
        }
        if (restart) start();
        return;
    }
    const auto ecards = app->get_ecards(Value("active"));
    Integer level = 0;
    for (const auto& ecard : ecards) if (ecard) level += ecard->m_level;
    if (!ecards.empty()) level /= static_cast<Integer>(ecards.size());
    const auto pcards = app->get_pcards(Value("active"));
    Integer success = 0;
    for (const auto& pcard : pcards) if (pcard) success += pcard->decide_outcome(level, {"agl", "trickish"}, static_cast<Integer>(ecards.size()) + 3, 0, 0) ? 1 : 0;
    if (!pcards.empty() && success > static_cast<Integer>(pcards.size()) / 2) {
        if (app->m_advlog) app->m_advlog->runaway(true);
        for (const auto& member : m_members) if (member) member->clear_action();
        app->play_sound("run", false, 100, 1, 0, 0, false);
        end();
        if (app->m_statusbar) app->m_statusbar->change(true, false);
    } else {
        if (app->m_advlog) app->m_advlog->runaway(false);
        app->play_sound("error", false, 100, 1, 0, 0, false);
        start();
    }
}

void BattleEngine::win(bool arg_runevent)
{
    const auto app = runtime();
    if (!app) return;
    for (const auto& member : app->get_pcards(Value{})) if (member) if (auto card = as_character(member)) card->clear_action();
    const auto starting = is_battlestarting();
    app->hide_cards(true, true, false, true, "", false);
    remove_menu_cards_from_group(app);
    app->m_mcards.clear();
    app->m_mcards_expandspchars.clear();
    app->m_file_updates.clear();
    if (const auto self = self_battle(this); self && app->m_advlog) app->m_advlog->end_battle(self);
    app->clear_battlearea(true, arg_runevent ? 1 : 0, false, starting);
}

void BattleEngine::defeat(bool arg_runevent)
{
    const auto app = runtime();
    if (!app) { m__running = false; m__ready = true; return; }
    for (const auto& member : m_members) if (member) member->clear_action();
    const auto starting = is_battlestarting();
    m__running = false;
    const auto events = scenario_events();
    const auto event = (arg_runevent && !app->is_forcegameover() && events) ? events->check_keynum(3) : std::nullopt;
    if (const auto self = self_battle(this); self && app->m_advlog) app->m_advlog->end_battle(self);
    if (event || !scenario_data() || !scenario_data()->can_gameover()) {
        app->hide_cards(true, true, false, true, "", false);
        remove_menu_cards_from_group(app);
        app->m_mcards.clear();
        app->m_mcards_expandspchars.clear();
        app->m_file_updates.clear();
        app->m__gameover = false;
        app->clear_battlearea(true, 3, false, starting);
    } else {
        app->set_gameover();
    }
}

bool BattleEngine::check_win()
{
    const auto app = runtime();
    if (!app) return false;
    for (const auto& ecard : app->get_ecards(Value{})) if (ecard && ecard->is_alive()) return false;
    return true;
}

bool BattleEngine::check_defeat()
{
    const auto data = scenario_data();
    if (!data || !data->can_gameover()) return false;
    const auto app = runtime();
    if (!app) return false;
    for (const auto& pcard : app->get_pcards(Value{})) if (pcard && pcard->is_alive()) return false;
    return true;
}

void BattleEngine::retain_member_iterations()
{
    // 入れ子のイベントで外側の走査が中断している場合がある。 共通の現
    // 在のメンバー一覧を置き換える前に、残っている各別参照を固定する。
    for (auto iteration = m_member_iteration; iteration; iteration = iteration->previous)
        if (!iteration->retained) iteration->retained = m_members;
}

void BattleEngine::set_members()
{
    m_pmembers.clear();
    m_emembers.clear();
    m_fmembers.clear();
    if (const auto app = runtime()) {
        for (const auto& card : app->get_pcards(Value("unreversed"))) if (card && card->m_is_entered_battle) if (auto c = as_character(card)) m_pmembers.push_back(c);
        for (const auto& card : app->get_ecards(Value("unreversed"))) if (card && card->m_is_entered_battle) if (auto c = as_character(card)) m_emembers.push_back(c);
        for (const auto& card : app->get_fcards(Value("unreversed"))) if (card && card->m_is_entered_battle) if (auto c = as_character(card)) m_fmembers.push_back(c);
    }
    retain_member_iterations();
    m_members = m_pmembers;
    m_members.insert(m_members.end(), m_emembers.begin(), m_emembers.end());
    m_members.insert(m_members.end(), m_fmembers.begin(), m_fmembers.end());
}

void BattleEngine::set_actionorder()
{
    // Pythonの bisect.insort は (-order, -index,
    // member) を使い、同じ行動順ならメンバー順を逆にする。 stable_sort
    // でもこの明示的な同順位の規則を保持し、安定性だけで-index順を代用しない。
    std::vector<std::tuple<Integer, Integer, std::shared_ptr<Character>>> ordered;
    ordered.reserve(m_members.size());
    for (Integer index = 0; index < static_cast<Integer>(m_members.size()); ++index) {
        const auto& member = m_members[static_cast<std::size_t>(index)];
        if (member) ordered.emplace_back(-member->decide_actionorder(), -index, member);
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
        return std::get<0>(left) < std::get<0>(right) ||
               (std::get<0>(left) == std::get<0>(right) && std::get<1>(left) < std::get<1>(right));
    });
    retain_member_iterations();
    m_members.clear();
    for (auto& item : ordered) m_members.push_back(std::get<2>(item));
}

void BattleEngine::set_action()
{
    for (const auto& member : m_pmembers) if (member) member->decide_action();
    const auto player_priority = m_priorityacts;
    const auto player_beast_priority = m_priorityacts_beast;
    clear_priorityacts();
    for (const auto& member : m_fmembers) if (member) member->decide_action();
    const auto friend_priority = m_priorityacts;
    const auto friend_beast_priority = m_priorityacts_beast;
    clear_priorityacts();
    for (const auto& member : m_emembers) if (member) member->decide_action();
    const auto enemy_priority = m_priorityacts;
    const auto enemy_beast_priority = m_priorityacts_beast;
    m_priorityacts = player_beast_priority;
    m_priorityacts.insert(m_priorityacts.end(), player_priority.begin(), player_priority.end());
    m_priorityacts.insert(m_priorityacts.end(), friend_beast_priority.begin(), friend_beast_priority.end());
    m_priorityacts.insert(m_priorityacts.end(), friend_priority.begin(), friend_priority.end());
    m_priorityacts.insert(m_priorityacts.end(), enemy_beast_priority.begin(), enemy_beast_priority.end());
    m_priorityacts.insert(m_priorityacts.end(), enemy_priority.begin(), enemy_priority.end());
    m_priorityacts_beast.clear();
}

void BattleEngine::clear_priorityacts()
{
    m_priorityacts.clear();
    m_priorityacts_beast.clear();
}

void BattleEngine::clear_playersaction()
{
    if (const auto app = runtime()) {
        for (const auto& card : app->get_pcards(Value{})) if (card) if (auto c = as_character(card)) c->clear_action();
        for (const auto& card : app->get_fcards(Value{})) if (card) if (auto c = as_character(card)) c->clear_action();
    }
}

::cw::lib::Value BattleEngine_run_lambda_line_145(::cw::lib::Value arg_fcard)
{
    // Pythonのassert用lambda。状態値をそのまま返し、呼出側のall相当を維持する。
    return arg_fcard;
}

::cw::lib::Value BattleEngine_ready_lambda_line_306(::cw::lib::Value arg_fcard)
{
    return arg_fcard;
}

::cw::lib::Value BattleEngine_update_showfcards_lambda_line_324(::cw::lib::Value arg_fcard)
{
    return arg_fcard;
}

::cw::lib::Value BattleEngine_runaway_lambda_line_336(::cw::lib::Value arg_fcard)
{
    return arg_fcard;
}

::cw::lib::Value BattleEngine_runaway_lambda_line_348(::cw::lib::Value arg_fcard)
{
    return arg_fcard;
}

void main()
{
    // Python版もpass。戦闘開始はCWPy::set_battleareaから行う。
}

}
