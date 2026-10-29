#pragma once

// 全ホストで使う共通スクロールバー表示。動作は scrollbar.cpp
// に実装し、OS 専用の仮実装にはしない。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include "cw/sprite/base.hpp"

namespace cw::sprite::scrollbar {
class ScrollBar;
struct ScrollRuntimeContext {
    std::function<::cw::lib::Real()> scale_factor{};
    std::function<::cw::lib::Integer()> mouse_y{};
};
void set_runtime_context(ScrollRuntimeContext* context);
class ScrollBar : public ::cw::sprite::base::CWPySprite,
                  public std::enable_shared_from_this<ScrollBar> {
public:
    ScrollBar() = default;
    virtual ~ScrollBar() = default;
    ::cw::lib::Integer m__skipcount{};
    std::optional<::cw::lib::Integer> m_lazypos_noscale{};
    std::function<void()> m_lazyscroll_func{};
    ::cw::lib::Rect m_track_rect{}, m_thumb_rect{};
    ::cw::lib::Integer m_scrpos_noscale{};
    ::cw::lib::Integer m_scrsize_noscale{};
    bool m_visible{};
    ::cw::lib::Integer m_width{};
    void initialize(::cw::lib::Integer arg_scrpos_noscale, ::cw::lib::Integer arg_scrsize_noscale, bool arg_visible);
    void scroll_to_mousepos(bool arg_lazy);
    void update_scale() override;
    void update(const ::cw::lib::Arguments& arg_args,
                const ::cw::lib::KeywordArguments& arg_kwargs) override;
    void update_lazyscroll();
    void set_pos(::cw::lib::Integer arg_scrpos_noscale, bool arg_lazy);
    ::cw::lib::Integer get_pos();
    void set_params(::cw::lib::Integer arg_scrpos_noscale, ::cw::lib::Integer arg_scrsize_noscale);
};

}

