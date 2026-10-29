#pragma once

#include "capabilities.hpp"
#include "file_picker.hpp"

#include <memory>

namespace cwxeditor::lib {

// 構成をEditorRuntimeContextから分離し、埋め込み
// ホストが画面・ブラウザーサービスを所有したまま この値だけを渡せる
// ようにする。ファクトリーは借用ホストのサービスを終了・確定しない。
struct FilePickerComposition {
    EditorFilePickerMode mode{EditorFilePickerMode::Unsupported};
    std::shared_ptr<DialogBackend> native_dialogs;
    WebFilePickerCallbacks web_callbacks;
    FilePickerCapabilities native_capabilities{FilePickerCapability::All};
    FilePickerCapabilities web_capabilities{FilePickerCapability::None};
};

// 選択したモードの完全な実装がなければnullptrを返す。 Browserモードはnative_dialogs
// があってもNativeFilePickerへ切り替えない。
[[nodiscard]] std::shared_ptr<FilePicker> make_file_picker(
    FilePickerComposition composition);

}  // namespace cwxeditor::lib
