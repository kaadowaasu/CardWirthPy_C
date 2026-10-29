#pragma once

#include "audio.hpp"

#include <memory>

namespace cwxeditor::lib {

/// Editorコンポーネントで使う、複数のプラットフォームに対応したSDL_mixer実装。
[[nodiscard]] std::shared_ptr<AudioBackend> make_sdl_audio_backend();

}  // namespace cwxeditor::lib
