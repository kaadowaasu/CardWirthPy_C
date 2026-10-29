#pragma once

// cw/sprite/bill.py の共通宣言。Surface の画素は SDL
// アダプターが担当し、Bill の状態・キャッシュ・倍率・詳細文は型付きで保持する。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include "cw/sprite/base.hpp"

namespace cw::sprite::bill {
using BillBitmapSet = std::tuple<
    std::vector<std::shared_ptr<::cw::lib::Surface>>,
    std::vector<std::shared_ptr<::cw::image::ImageInfo>>>;

class Bill : public ::cw::lib::Object, public std::enable_shared_from_this<Bill> {
public:
    Bill() = default;
    virtual ~Bill() = default;
    std::map<::cw::lib::Real, std::shared_ptr<::cw::lib::Surface>> m__bgs{};
    std::map<::cw::lib::Real, BillBitmapSet> m__bmps{};
    ::cw::lib::Integer m__xp_noscale{};
    ::cw::lib::Integer m__yp_noscale{};
    std::shared_ptr<::cw::header::ScenarioHeader> m_header{};
    ::cw::lib::Rect m_rect_noscale{};
    std::vector<std::shared_ptr<::cw::sprite::message::SelectionBar>> m_selections{};
    std::map<::cw::lib::String, std::tuple<std::shared_ptr<::cw::lib::Surface>, bool>> m_specialchars{};
    void initialize(std::shared_ptr<::cw::header::ScenarioHeader> arg_header);
    std::tuple<std::shared_ptr<::cw::lib::Surface>, ::cw::lib::Rect> create_image();
    ::cw::lib::Integer get_height_noscale();
    std::shared_ptr<::cw::sprite::bill::BillSprite> create_message();
};

class BillSprite : public ::cw::sprite::base::CWPySprite {
public:
    BillSprite() = default;
    virtual ~BillSprite() = default;
    std::shared_ptr<::cw::sprite::bill::Bill> m_bill{};
    ::cw::lib::Rect m_rect_noscale{};
    std::vector<std::shared_ptr<::cw::sprite::message::SelectionBar>> m_selections{};
    std::map<::cw::lib::String, std::tuple<std::shared_ptr<::cw::lib::Surface>, bool>> m_specialchars{};
    void initialize(std::shared_ptr<::cw::sprite::bill::Bill> arg_bill);
    void set_backlog_position(::cw::lib::Integer arg_y_noscale);
    void update_scale();
};

::cw::lib::String get_detailtext(std::shared_ptr<::cw::header::ScenarioHeader> arg_header);
void main();
}

