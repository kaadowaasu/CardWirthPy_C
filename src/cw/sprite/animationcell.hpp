#pragma once

// cw/sprite/animationcell.py の移植。画素の格納は共通化
// し、登録、レイヤー同期、無効化は SpriteUiBackend が担当する。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"
#include "cw/sprite/base.hpp"

namespace cw::sprite::animationcell {
using Dimension = std::variant<::cw::lib::Integer, ::cw::lib::Real, ::cw::lib::String>;
class _AnimationPart : public ::cw::lib::Object, public std::enable_shared_from_this<_AnimationPart> {
public:
    _AnimationPart() = default;
    virtual ~_AnimationPart() = default;
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> m__fill_color{};
    bool m__has_alpha{};
    std::shared_ptr<::cw::lib::Surface> m__image{};
    std::tuple<::cw::lib::String, ::cw::lib::String, ::cw::lib::String, ::cw::lib::String> m__rect_str{};
    ::cw::lib::Integer m_animation_frame{};
    ::cw::lib::String m_animation_type{};
    std::vector<::cw::lib::Integer> m_dealing_scales{};
    ::cw::lib::Integer m_duration{};
    std::optional<std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer>> m_fill_color{};
    ::cw::lib::String m_id{};
    std::shared_ptr<::cw::lib::Surface> m_image{};
    std::shared_ptr<::cw::lib::Surface> m_image_noscale{};
    ::cw::lib::String m_imgpath{};
    bool m_mask{};
    bool m_parallel{};
    std::weak_ptr<::cw::sprite::animationcell::AnimationCell> m_parent{};
    std::vector<std::shared_ptr<::cw::sprite::animationcell::_AnimationPart>> m_parts{};
    std::tuple<Dimension, Dimension> m_pos_noscale{};
    ::cw::lib::Rect m_rect{};
    ::cw::lib::Integer m_repeat_count{};
    ::cw::lib::Integer m_repeat_interval{};
    std::tuple<Dimension, Dimension> m_size_noscale{};
    ::cw::lib::Integer m_spawn{};
    ::cw::lib::Integer m_startframe{};
    ::cw::lib::String m_type{};
    void initialize(std::shared_ptr<::cw::sprite::animationcell::AnimationCell> arg_parent, std::shared_ptr<::cw::data::CWPyElement> arg_data, ::cw::lib::Integer arg_startframe);
    void load_cell();
    void update_scale();
    void clear_image();
    void update_image();
};

class AnimationCell : public ::cw::sprite::base::SelectableSprite, public std::enable_shared_from_this<AnimationCell> {
public:
    AnimationCell() = default;
    virtual ~AnimationCell();
    std::map<::cw::lib::String, std::shared_ptr<::cw::sprite::animationcell::_AnimationPart>> m_animation_table{};
    std::shared_ptr<::cw::sprite::animationcell::_AnimationPart> m_animations{};
    std::map<std::tuple<::cw::lib::String, ::cw::lib::Real>, std::shared_ptr<::cw::lib::Surface>> m_cache{};
    // スタック上に所有するセルを使う場合、所有権のない自身の別名参照を
    // 保持する。共有インスタンスは enable_shared_from_this
    // を直接使い、循環参照を防ぐためここは空にする。
    std::shared_ptr<AnimationCell> m_self_alias{};
    ::cw::lib::Integer m_end_frame{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> m_pos_noscale{};
    std::vector<std::shared_ptr<::cw::sprite::animationcell::_AnimationPart>> m_refs{};
    std::tuple<::cw::lib::Integer, ::cw::lib::Integer> m_size_noscale{};
    // 直近のアニメーション更新で変わった正確な画面
    // 座標の領域。制御側は毎フレーム 632x420
    // のセル全体を無効にせず、この領域を使う。
    std::optional<::cw::lib::Rect> m_frame_dirty{};
    void initialize(::cw::lib::String arg_path, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_size_noscale, std::tuple<::cw::lib::Integer, ::cw::lib::Integer> arg_pos_noscale, std::shared_ptr<::cw::lib::Object> arg_spritegrp, ::cw::lib::Integer arg_layer);
    std::vector<std::shared_ptr<::cw::sprite::animationcell::_AnimationPart>> _iter_animes(std::shared_ptr<::cw::sprite::animationcell::_AnimationPart> arg_anime);
    void update_scale();
    void quit();
    void update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs);
    void update_normal();
    void lclick_event();
    void rclick_event();
    void update_animation();
};

void main();
}

