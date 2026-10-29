#pragma once

// cw/sprite/touchbutton.py の移植。SDL 固有の文字・アイコン
// 描画を Surface の抽象化に分離し、入力・状態処理を独立して使えるようにする。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include "cw/sprite/base.hpp"

namespace cw::sprite::touchbutton {
struct TouchRuntimeContext {
    std::function<void()> hide_touchbuttons{};
    std::function<void()> update_tiles{};
    std::function<void(::cw::lib::Integer)> move_selection{};
    std::function<::cw::lib::Real()> master_volume{};
    std::function<void(::cw::lib::Real)> set_master_volume{};
    std::function<bool()> can_select{};
};
void set_runtime_context(TouchRuntimeContext* context);
class TouchButton : public ::cw::sprite::base::SelectableSprite {
public:
    TouchButton() = default;
    virtual ~TouchButton() = default;
    std::shared_ptr<::cw::lib::Surface> m__disabledimage{},m__selectedimage{},m__unselectedimage{};
    std::optional<::cw::lib::Integer> m__shift_start_top{};
    ::cw::lib::String m_desc{},m_hotkey{},m_name{};
    ::cw::lib::Callback m_func{},m_is_enabled{};
    std::shared_ptr<::cw::lib::Surface> m_icon{};
    ::cw::lib::Integer m_shift_top{},m_width{};
    void initialize(std::optional<std::shared_ptr<::cw::lib::Surface>> arg_icon, ::cw::lib::String arg_name, ::cw::lib::String arg_desc, ::cw::lib::String arg_hotkey, ::cw::lib::Callback arg_func, ::cw::lib::Callback arg_is_enabled, ::cw::lib::Integer arg_width);
    void update_scale() override;
    std::shared_ptr<::cw::lib::Surface> get_selectedimage() override;
    std::shared_ptr<::cw::lib::Surface> get_unselectedimage() override;
    virtual void update_image();
    static ::cw::lib::Integer calc_width(std::optional<std::shared_ptr<::cw::lib::Surface>> arg_icon, ::cw::lib::String arg_name, ::cw::lib::String arg_desc, ::cw::lib::String arg_hotkey);
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs) override;
    void update_selection() override;
    virtual void update_shiftup();
    void lclick_event() override;
    void rclick_event() override;
};

class PointableTile : public ::cw::sprite::touchbutton::TouchButton {
public:
    PointableTile() = default;
    virtual ~PointableTile() = default;
    void initialize(std::shared_ptr<::cw::lib::Surface> arg_icon, ::cw::lib::String arg_name, ::cw::lib::String arg_desc, ::cw::lib::String arg_hotkey, ::cw::lib::Callback arg_func, ::cw::lib::Callback arg_is_enabled, ::cw::lib::Integer arg_width);
    void update_selection() override;
    bool is_selection() override;
};

class VolumeTile : public ::cw::sprite::touchbutton::TouchButton {
public:
    VolumeTile() = default;
    virtual ~VolumeTile() = default;
    ::cw::lib::Rect m_padrect{};
    ::cw::lib::Integer m_volume_percent{};
    void initialize(::cw::lib::Integer arg_width);
    void update_scale() override;
    void update_image() override;
    void update_selection() override;
    void ldown_event() override;
    void _moved();
    void lclick_event() override;
};

class SimplePointableTile : public ::cw::sprite::touchbutton::PointableTile {
public:
    SimplePointableTile() = default;
    virtual ~SimplePointableTile() = default;
    void initialize(std::shared_ptr<::cw::lib::Surface> arg_icon, ::cw::lib::String arg_name, ::cw::lib::Callback arg_func, ::cw::lib::Callback arg_is_enabled, ::cw::lib::Integer arg_width);
    void update_scale() override;
};

class SwitchSpriteTile : public ::cw::sprite::touchbutton::PointableTile {
public:
    SwitchSpriteTile() = default;
    virtual ~SwitchSpriteTile() = default;
    ::cw::lib::Integer m_move_count{};
    void initialize(std::shared_ptr<::cw::lib::Surface> arg_icon, ::cw::lib::Integer arg_move_count, ::cw::lib::Integer arg_width);
    void update_scale() override;
    void lclick_event() override;
};

::cw::lib::Integer _calc_singlelinetileheight();
bool can_selectsprite();
void main();
}

