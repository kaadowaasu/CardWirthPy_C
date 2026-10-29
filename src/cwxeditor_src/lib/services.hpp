#pragma once

#include "archive.hpp"
#include "audio.hpp"
#include "clipboard.hpp"
#include "dialogs.hpp"
#include "drag_drop.hpp"
#include "filesystem.hpp"
#include "file_picker.hpp"
#include "graphics.hpp"
#include "gui.hpp"
#include "ipc.hpp"
#include "process.hpp"
#include "shell.hpp"
#include "system.hpp"
#include "text.hpp"
#include "threading.hpp"

#include <memory>

namespace cwxeditor::lib {

struct Services {
    std::shared_ptr<GuiBackend> gui;
    std::shared_ptr<GraphicsBackend> graphics;
    std::shared_ptr<ImageCodecBackend> image_codecs;
    std::shared_ptr<DialogBackend> dialogs;
    // 型付きのドキュメント/プロバイダー契約。dialogsは従来の値
    // 入力ダイアログ用に残す。ファイル操作はこれを使い、Web/SAF
    // の利用権とネイティブパスで取消・エラー・完了の意味を統一する。
    std::shared_ptr<FilePicker> file_picker;
    std::shared_ptr<ClipboardBackend> clipboard;
    std::shared_ptr<DragDropBackend> drag_drop;
    std::shared_ptr<AudioBackend> audio;
    std::shared_ptr<FileSystemBackend> filesystem;
    std::shared_ptr<ProcessBackend> processes;
    std::shared_ptr<ShellBackend> shell;
    std::shared_ptr<IpcBackend> ipc;
    std::shared_ptr<ArchiveBackend> archives;
    std::shared_ptr<SystemBackend> system;
    std::shared_ptr<TextBackend> text;
    std::shared_ptr<MainDispatcher> dispatcher;
    std::shared_ptr<TaskBackend> tasks;
};

Services make_headless_services();

// 製品のGUI表示はWindowsを含む全OSでSDLを通じて共有する
// 。ダイアログ呼び出しは共通UIディスパッチャーを通し、ネイティブの選
// 択画面と実行時サービスはプラットフォームのアダプター内に置く。処理
// 側はServicesだけを使い、OS固有のヘッダーを取り込まない。
Services make_platform_services();

}  // namespace cwxeditor::lib
