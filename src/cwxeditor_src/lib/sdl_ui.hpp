#pragma once

#include "lifecycle.hpp"
#include "services.hpp"

#include <functional>
#include <string_view>

namespace cwxeditor::lib {

// 単体版の画面表示規則。組み込みEditorは借用したレンダラーの表示
// を行わず、合成を担当するホストが最後のPresentを一度だけ呼ぶ。
enum class EditorPresentMode {
    VSync,
    Adaptive,
    Immediate,
};

// 空の値では、単体版Editorの低遅延な既定値としてImmediateを選ぶ。
// Adaptiveの明示指定では間に合ったフレームの同期を維持し、SDLドライ
// バーが遅延スワップ時のティアリングに対応しない場合はVSyncに切り替える。
[[nodiscard]] EditorPresentMode parse_editor_present_mode(
    std::string_view value) noexcept;
[[nodiscard]] EditorPresentMode editor_present_mode_from_environment() noexcept;

// 共通表示側がIMEの未確定文字列を描画することをSDLに伝える。最初の入力欄
// にフォーカスする前ではなく、SDLの映像サブシステムの初期化前に実行する必要
// がある。遅れるとWindowsがネイティブの変換UIを維持し、インライン表
// 示用のSDL_EVENT_TEXT_EDITINGを通知しない場合がある。
void configure_sdl_ui_before_init();

// 全OSのEditorで共用する単一のUI表示実装を設定する。OSやホス
// トの違いはパラメーターとサービスアダプターで扱い、別のUIは作らない。
void install_sdl_ui_services(Services& services);
void invalidate_sdl_renderer_resources(Services& services);

// SDLのバックグラウンド移行・復帰・メモリ関連イベントを単体版Editor
// の実行環境へ接続する。注入されたGUIがSDLを使わない場合は何もしない。
void set_sdl_application_lifecycle_handler(
    Services& services, std::function<void(LifecycleEvent)> handler);

}  // namespace cwxeditor::lib
