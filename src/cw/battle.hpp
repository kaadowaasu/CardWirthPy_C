#pragma once

// cw/battle.py の戦闘状態遷移を移植した宣言。
#include <exception>
#include <memory>
#include <optional>
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"

namespace cw::battle {
class BattleEngine : public ::cw::lib::Object {
public:
    BattleEngine() = default;
    virtual ~BattleEngine() = default;
    inline static bool s_in_roundevent{};
    inline static bool s_possible_runaway{};
    inline static ::cw::lib::Integer s_round{};
    bool m__ranaway{};
    bool m__ready{};
    bool m__running{};
    ::cw::lib::Value m_data{};
    std::shared_ptr<::cw::data::CWPyElement> m_data_typed{};
    std::vector<std::shared_ptr<::cw::character::Character>> m_emembers{};
    std::vector<std::shared_ptr<::cw::character::Character>> m_fmembers{};
    bool m_in_roundevent{};
    std::vector<std::shared_ptr<::cw::character::Character>> m_members{};
    ::cw::lib::Integer m_numenemy{};
    std::vector<std::shared_ptr<::cw::character::Character>> m_pmembers{};
    bool m_possible_runaway{};
    std::vector<std::tuple<::cw::lib::String, ::cw::lib::Value, std::shared_ptr<::cw::character::Character>>> m_priorityacts{};
    std::vector<std::tuple<::cw::lib::String, ::cw::lib::Value, std::shared_ptr<::cw::character::Character>>> m_priorityacts_beast{};
    ::cw::lib::Integer m_round{};
    static void initialize_card_variables(::cw::lib::String arg_initialization);
    void initialize(std::shared_ptr<::cw::data::CWPyElement> arg_data);
    bool is_running();
    bool is_ready();
    bool is_battlestarting();
    void start();
    void process_exception(std::shared_ptr<::cw::battle::BattleError> arg_ex);
    void run();
    void end(bool arg_areachange = true, bool arg_f9 = false, bool arg_startnextbattle = false);
    void ready(bool arg_redraw = true);
    void update_debug();
    void update_showfcards();
    void runaway();
    void win(bool arg_runevent = true);
    void defeat(bool arg_runevent = true);
    bool check_win();
    bool check_defeat();
    void set_members();
    void set_actionorder();
    void set_action();
    void clear_priorityacts();
    void clear_playersaction();
private:
    using Members = std::vector<std::shared_ptr<::cw::character::Character>>;
    struct MemberIteration {
        std::optional<Members> retained;
        std::shared_ptr<MemberIteration> previous;
    };
    std::shared_ptr<MemberIteration> m_member_iteration;
    void retain_member_iterations();
};

class BattleError : public ::cw::lib::Object, public std::exception {
public:
    BattleError() = default;
    virtual ~BattleError() = default;
    const char* what() const noexcept override { return "battle error"; }
};

class BattleAreaChangeError : public ::cw::battle::BattleError {
public:
    BattleAreaChangeError() = default;
    virtual ~BattleAreaChangeError() = default;
    const char* what() const noexcept override { return "battle area changed"; }
};

class BattleDefeatError : public ::cw::battle::BattleError {
public:
    BattleDefeatError() = default;
    virtual ~BattleDefeatError() = default;
    const char* what() const noexcept override { return "battle defeat"; }
};

class BattleStartBattleError : public ::cw::battle::BattleError {
public:
    BattleStartBattleError() = default;
    virtual ~BattleStartBattleError() = default;
    const char* what() const noexcept override { return "start next battle"; }
};

class BattleWinError : public ::cw::battle::BattleError {
public:
    BattleWinError() = default;
    virtual ~BattleWinError() = default;
    const char* what() const noexcept override { return "battle win"; }
};

::cw::lib::Value BattleEngine_run_lambda_line_145(::cw::lib::Value arg_fcard);
::cw::lib::Value BattleEngine_ready_lambda_line_306(::cw::lib::Value arg_fcard);
::cw::lib::Value BattleEngine_update_showfcards_lambda_line_324(::cw::lib::Value arg_fcard);
::cw::lib::Value BattleEngine_runaway_lambda_line_336(::cw::lib::Value arg_fcard);
::cw::lib::Value BattleEngine_runaway_lambda_line_348(::cw::lib::Value arg_fcard);
void main();
}

