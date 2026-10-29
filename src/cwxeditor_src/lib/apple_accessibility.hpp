#pragma once

#include "ui_accessibility.hpp"

#include <memory>
#include <string_view>

namespace cwxeditor::lib {

// SDLはEditorを一枚のキャンバスとして描くため、AppKit/UIKit
// は論理的な入力部品を発見できない。 Androidの接
// 続と同じフォーカス通知を、VoiceOverへ送る。
void announce_apple_accessibility(std::string_view name,
                                  std::string_view description);

// Apple側がネイティブのアクセシビリティ項目を管理し、共通
// ・SDL側には転送インターフェースだけを公開する。 論理ルー
// トは弱参照で保存し、操作を送る前に画面トークンを確認する。
[[nodiscard]] std::shared_ptr<PlatformAccessibilityBackend>
make_apple_accessibility_backend();

}  // namespace cwxeditor::lib
