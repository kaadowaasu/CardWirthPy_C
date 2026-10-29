#pragma once

// cw/deck.pyの移植。配列でPythonのhand・talon
// ・nextcardsと、使用済み・捨て札の状態を保持する。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"

namespace cw::deck {
class Deck : public ::cw::lib::Object {
public:
    Deck() = default;
    virtual ~Deck() = default;
    bool m__throwaway{};
    std::optional<std::shared_ptr<::cw::header::CardHeader>> m__used{};
    std::vector<std::shared_ptr<::cw::header::CardHeader>> m_hand{};
    std::vector<::cw::lib::Integer> m_nextcards{};
    std::vector<std::shared_ptr<::cw::header::CardHeader>> m_talon{};
    void initialize(std::shared_ptr<::cw::character::Character> arg_ccard);
    std::vector<std::shared_ptr<::cw::header::CardHeader>> get_hand(std::shared_ptr<::cw::character::Character> arg_ccard);
    std::optional<std::shared_ptr<::cw::header::CardHeader>> get_used();
    void clear_used();
    std::vector<std::shared_ptr<::cw::header::CardHeader>> get_actioncards(std::shared_ptr<::cw::character::Character> arg_ccard);
    void update_actioncards(std::shared_ptr<::cw::character::Character> arg_ccard);
    std::vector<std::shared_ptr<::cw::header::CardHeader>> get_skillcards(std::shared_ptr<::cw::character::Character> arg_ccard, std::optional<std::map<std::shared_ptr<::cw::header::CardHeader>, ::cw::lib::Integer>> arg_handcounts);
    void set_nextcard(::cw::lib::Integer arg_resid = 0);
    bool _set_nextcard(std::shared_ptr<::cw::character::Character> arg_ccard, ::cw::lib::Integer arg_resid);
    void shuffle();
    ::cw::lib::Integer get_handmaxnum(std::shared_ptr<::cw::character::Character> arg_ccard);
    void set(std::shared_ptr<::cw::character::Character> arg_ccard, bool arg_draw = true);
    void set_hand(std::shared_ptr<::cw::character::Character> arg_ccard);
    void add(std::shared_ptr<::cw::character::Character> arg_ccard, std::shared_ptr<::cw::header::CardHeader> arg_header, bool arg_is_replace = false);
    void remove(std::shared_ptr<::cw::character::Character> arg_ccard, std::shared_ptr<::cw::header::CardHeader> arg_header);
    void get_skillpower(std::shared_ptr<::cw::character::Character> arg_ccard);
    void lose_skillpower(std::shared_ptr<::cw::character::Character> arg_ccard, ::cw::lib::Integer arg_losevalue);
    void _update_skillpower(std::shared_ptr<::cw::character::Character> arg_ccard);
    void update_skillcardimage(std::shared_ptr<::cw::header::CardHeader> arg_header);
    void clear(std::shared_ptr<::cw::character::Character> arg_ccard);
    void throwaway();
    void clear_nextcards();
    bool is_throwed();
    void _remove(std::shared_ptr<::cw::header::CardHeader> arg_header);
    void _clear_hand();
    void draw(std::shared_ptr<::cw::character::Character> arg_ccard);
    ::cw::lib::Integer check_mind(std::shared_ptr<::cw::character::Character> arg_ccard, ::cw::lib::Integer arg_skill_num);
    void set_used(std::shared_ptr<::cw::header::CardHeader> arg_header);
    void use(std::shared_ptr<::cw::header::CardHeader> arg_header, bool arg_consumecard);
};

void main();
}

