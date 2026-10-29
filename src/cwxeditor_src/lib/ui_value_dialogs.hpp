#pragma once

#include "components.hpp"
#include "dialogs.hpp"
#include "file_picker.hpp"

namespace cwxeditor::lib {

// プラットフォーム非依存のトランザクション型選択画面。OSハンドルやイベント
// ループは持たない。呼び出し側は通常のGuiBackendのモーダル経路で表
// 示し、selected()を読む。取消や終了では編集中の値を公開しない。
template<class Value> struct ValueDialogPresentation {
    std::shared_ptr<Dialog> dialog;
    std::function<std::optional<Value>()> selected;
};

[[nodiscard]] ValueDialogPresentation<Color> make_color_selection_dialog(
    Color initial, const std::shared_ptr<Window>& owner = {});
[[nodiscard]] ValueDialogPresentation<FontSpec> make_font_selection_dialog(
    const FontSpec& initial, const std::shared_ptr<Window>& owner = {});
[[nodiscard]] ValueDialogPresentation<MessageResult> make_message_dialog(
    const MessageDialogRequest& request, const std::shared_ptr<Window>& owner = {});
// モバイルの選択画面は、記憶したパスやシンボリックリンクが外部を指す場合も、
// 設定されたユーザーマウント内に限定する。選択操作自体は書き込みを行わない。
[[nodiscard]] ValueDialogPresentation<std::vector<std::filesystem::path>> make_stored_file_dialog(
    const std::filesystem::path& root, FilePickerOperation operation,
    const FileDialogRequest& request, const std::shared_ptr<Window>& owner = {},
    // 保存時だけ使用し、ダイアログで選択したファイル形式をrequest.filters
    // のインデックスで受け取る。選択肢がある場合は形式の選択欄を表示する。
    std::shared_ptr<std::optional<std::size_t>> selected_filter = {});
// 保存ファイルのダイアログのフォルダー行に表示するアイ
// コン（Editorのfolder.png）。資源の所
// 有元が公開し、解放前に0へ戻す。UIスレッド専用。
void set_stored_file_dialog_folder_icon(ResourceId icon) noexcept;

} // namespace cwxeditor::lib
