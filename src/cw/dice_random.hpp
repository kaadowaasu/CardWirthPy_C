#pragma once
#include "cw/lib/compat.hpp"
#include <vector>

namespace cw::dice {
// Pythonのモジュール単位のrandom.random/random.sample
// に対応するネイティブの入口。 すべての環境でDiceと同じ乱数生成器を使う。
::cw::lib::Real random_fraction();
std::size_t choice_index(std::size_t count);
std::vector<::cw::lib::Value> sample_values(
    std::vector<::cw::lib::Value> population, ::cw::lib::Integer count);
}
