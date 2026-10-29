#pragma once

#include "services.hpp"

namespace cwxeditor::lib {

// ビルド対象として選択されたプラットフォームを返す。GUI
// とシステムのアダプターはこの判定を共有し、モバイル・Web
// ・デスクトップで機能報告が食い違わないようにする。
[[nodiscard]] Platform native_platform() noexcept;

// テスト用・ヘッドレス用の実行時アダプターを、SDLのモバイル/Apple版Editor
// で安全に使用できるファイルシステム、OS、シェル、テキストのサービスに置き換える。
void install_portable_runtime_services(Services& services);

}  // namespace cwxeditor::lib
