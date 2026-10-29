#pragma once

// cw/dice.pyに対応する
// 、ダイスと無作為選択の宣言。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"

namespace cw::dice {
class Dice : public ::cw::lib::Object {
public:
    Dice() = default;
    virtual ~Dice() = default;
    ::cw::lib::Integer roll(::cw::lib::Integer arg_times = 1, ::cw::lib::Integer arg_sided = 6);
    std::optional<::cw::lib::Value> choice(std::vector<::cw::lib::Value> arg_seq);
    ::cw::lib::Value choice_exists(std::vector<::cw::lib::Value> arg_seq);
    std::vector<::cw::lib::Value> shuffle(std::vector<::cw::lib::Value> arg_seq);
    std::optional<::cw::lib::Value> pop(std::vector<::cw::lib::Value>& arg_seq);
};

extern ::cw::lib::Value g__T;

void main();
}

