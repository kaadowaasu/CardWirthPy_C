#pragma once

// cw/animation.py の同期／非同期アニメーション制御をSDL非依存で公開する。
#include "cw/lib/compat.hpp"
#include "cw/fwd.hpp"

namespace cw::animation {
bool animate_sprite(std::shared_ptr<::cw::sprite::base::CWPySprite> arg_sprite, ::cw::lib::String arg_anitype, bool arg_clearevent = true, bool arg_background = false, bool arg_statusbutton = false, bool arg_battlespeed = false);
void animate_sprites(std::vector<std::shared_ptr<::cw::sprite::base::CWPySprite>> arg_sprites, ::cw::lib::String arg_anitype, bool arg_clearevent = true, bool arg_battlespeed = false);
bool animate_sprites2(std::vector<std::tuple<std::shared_ptr<::cw::sprite::base::CWPySprite>, ::cw::lib::String>> arg_sprandanimes, bool arg_clearevent = true, bool arg_battlespeed = false);
std::optional<std::shared_ptr<::cw::lib::Object>> _inputevent(std::optional<std::shared_ptr<::cw::lib::Object>> arg_clip, bool arg_clearevent, bool arg_statusbutton);
void start_animation(std::shared_ptr<::cw::sprite::base::CWPySprite> arg_sprite, ::cw::lib::String arg_anitype);
bool _get_skipstatus(bool arg_clearevent);
::cw::lib::String sprite_status(const std::shared_ptr<::cw::sprite::base::CWPySprite>& arg_sprite);
void set_sprite_frame(const std::shared_ptr<::cw::sprite::base::CWPySprite>& arg_sprite, ::cw::lib::Integer arg_frame);
bool call_named_update(const std::shared_ptr<::cw::sprite::base::CWPySprite>& arg_sprite);
void main();
}

