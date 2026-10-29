#pragma once

// cw/sprite/transition.py の移植。型付きのアニメ
// ーション状態を SDL のテクスチャー・描画アダプターから分離する。
#include "cw/lib/compat.hpp"
#include "cw/lib/python_integer_value.hpp"
#include "cw/fwd.hpp"
#include "cw/sprite/base.hpp"

namespace cw::sprite::transition {
struct TransitionRuntimeContext {
    std::function<::cw::lib::String()> default_name{};
    std::function<::cw::lib::Integer()> default_speed{};
    std::function<::cw::lib::Integer(::cw::lib::Integer)> scale{};
    std::function<std::shared_ptr<::cw::lib::Surface>()> capture_background{};
    std::function<void(std::vector<::cw::lib::Point>&)> shuffle{};
};
void set_runtime_context(TransitionRuntimeContext* context);
class Transition : public ::cw::sprite::base::CWPySprite {
public:
    Transition() = default;
    virtual ~Transition() = default;
    ::cw::lib::PythonInteger m_speed{};
    ::cw::lib::Integer m_global_alpha{255};
    void initialize(std::shared_ptr<::cw::lib::Surface> arg_bgscr, ::cw::lib::Integer arg_speed);
    void initialize(std::shared_ptr<::cw::lib::Surface> arg_bgscr, ::cw::lib::PythonInteger arg_speed);
    void update_scale() override;
    void clear();
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs) override;
    virtual void update_transition();
};

class Blinds : public ::cw::sprite::transition::Transition {
public:
    Blinds() = default;
    virtual ~Blinds() = default;
    ::cw::lib::Color m_changecolor{255,255,255,0};
    ::cw::lib::Integer m_num_split{};
    std::vector<::cw::lib::Point> m_poslist{};
    ::cw::lib::Rect m_rect_blinds{};
    ::cw::lib::PythonInteger m_variation{};
    ::cw::lib::Integer m_w_blinds{};
    void initialize(std::shared_ptr<::cw::lib::Surface> arg_bgscr, ::cw::lib::Integer arg_speed);
    void initialize(std::shared_ptr<::cw::lib::Surface> arg_bgscr, ::cw::lib::PythonInteger arg_speed);
    void update_transition() override;
};

class Fade : public ::cw::sprite::transition::Transition {
public:
    Fade() = default;
    virtual ~Fade() = default;
    ::cw::lib::Integer m_m_frame{};
    ::cw::lib::PythonInteger m_variation{};
    void initialize(std::shared_ptr<::cw::lib::Surface> arg_bgscr, ::cw::lib::Integer arg_speed);
    void initialize(std::shared_ptr<::cw::lib::Surface> arg_bgscr, ::cw::lib::PythonInteger arg_speed);
    void update_transition() override;
};

class PixelDissolve : public ::cw::sprite::transition::Transition {
public:
    PixelDissolve() = default;
    virtual ~PixelDissolve() = default;
    ::cw::lib::Color m_changecolor{255,255,255,0};
    std::vector<::cw::lib::Point> m_poslist{};
    ::cw::lib::Rect m_rect_sec{};
    ::cw::lib::Integer m_sec_h{};
    ::cw::lib::Integer m_sec_h_noscale{};
    ::cw::lib::Integer m_sec_w{};
    ::cw::lib::Integer m_sec_w_noscale{};
    ::cw::lib::PythonInteger m_variation{};
    void initialize(std::shared_ptr<::cw::lib::Surface> arg_bgscr, ::cw::lib::Integer arg_speed);
    void initialize(std::shared_ptr<::cw::lib::Surface> arg_bgscr, ::cw::lib::PythonInteger arg_speed);
    void update_transition() override;
};

std::optional<std::shared_ptr<::cw::sprite::transition::Transition>> get_transition(std::tuple<::cw::lib::String, ::cw::lib::Value> arg_name_and_speed);
void main();
}

